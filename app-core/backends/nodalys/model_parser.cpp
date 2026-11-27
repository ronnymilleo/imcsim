/**
 * @file    model_parser.cpp
 * @brief   Turns the .model text of a library model into the parameter structs Nodalys evaluates.
 */

#include "backends/nodalys/model_parser.h"

#include "circuit/circuit.h"
#include "circuit/library_model.h"
#include "circuit/spice_library.h"
#include "spice/spice_text.h"
#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace Core {

namespace {

bool IsSeparator(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0 || character == '+';
}

bool IsKeyCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

/**
 * @brief   Whether a character belongs to a value: anything up to the next space or the closing parenthesis.
 */
bool IsValueCharacter(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) == 0 && character != ')';
}

/**
 * @brief   Moves past the characters of the text that a predicate accepts.
 * @param[in] text       Text to scan.
 * @param[in] position   Where to start.
 * @param[in] is_inside  Predicate of the characters to move past.
 * @return  The position of the first character it does not accept, or the end of the text.
 */
std::size_t SkipWhile(const std::string &text, std::size_t position, bool (*is_inside)(char)) {
    while (position < text.size() && is_inside(text[position])) {
        ++position;
    }
    return position;
}

/**
 * @brief   Splits the parameter list of a ".model" statement into upper-case keys and their value text.
 * @details A token that is not a KEY=VALUE pair is skipped.
 * @param[in] text  Whole model statement, continuation lines included.
 * @return  Key to value text, such as "IS" → "2.52n"; empty when the statement has no parameter list.
 */
std::map<std::string, std::string> ParameterList(const std::string &text) {
    const std::size_t open = text.find('(');
    std::map<std::string, std::string> values;
    if (open == std::string::npos) {
        return values;
    }
    std::size_t position = open + 1;
    while (position < text.size() && text[position] != ')') {
        const std::size_t key_start = SkipWhile(text, position, IsSeparator);
        const std::size_t key_end = SkipWhile(text, key_start, IsKeyCharacter);
        const bool is_pair = key_end > key_start && key_end < text.size() && text[key_end] == '=';
        const std::size_t value_start = is_pair ? key_end + 1 : key_end;
        position = SkipWhile(text, value_start, IsValueCharacter);
        if (is_pair) {
            values[ToUpper(text.substr(key_start, key_end - key_start))] =
                text.substr(value_start, position - value_start);
        }
    }
    return values;
}

/**
 * @brief   Reads one numeric model parameter as SPICE writes it.
 * @param[in] model  Model the parameter belongs to, for the error message.
 * @param[in] key    Upper-case parameter name.
 * @param[in] text   Value text.
 * @return  The value, or the reason Nodalys cannot read it.
 */
std::expected<double, std::string> Number(const LibraryModel &model, const std::string &key, const std::string &text) {
    if (text.starts_with('{')) {
        return std::unexpected(std::format("Nodalys cannot evaluate the expression {} of the parameter {} "
                                           "in the model {}",
                                           text,
                                           key,
                                           model.Name));
    }
    if (const std::optional<double> parsed = ParseSpiceNumber(text)) {
        return *parsed;
    }
    return std::unexpected(
        std::format("Nodalys cannot read {} as the parameter {} in the model {}", text, key, model.Name));
}

/**
 * @brief   Looks up the library model a part names and checks it fits the part's family.
 * @param[in] circuit  Circuit that carries the library models.
 * @param[in] part     Part naming the model.
 * @return  The model, or the reason it cannot be used.
 */
std::expected<const LibraryModel *, std::string> FindModel(const Circuit &circuit, const Component &part) {
    const LibraryModel *model = circuit.FindLibraryModel(part.GetLibraryModel());
    if (model == nullptr) {
        return std::unexpected(std::format("{} uses the model {}, which the circuit does not have",
                                           part.GetName(),
                                           part.GetLibraryModel()));
    }
    if (!CanUseLibraryModel(part.GetType(), model->Kind)) {
        return std::unexpected(
            std::format("{} cannot use the {} model {}", part.GetName(), GetModelKindName(model->Kind), model->Name));
    }
    return model;
}

std::expected<void, std::string> CheckMosfetLevel(const LibraryModel &model,
                                                  const std::map<std::string, std::string> &values) {
    if (const auto level = values.find("LEVEL"); level != values.end()) {
        const auto number = Number(model, "LEVEL", level->second);
        if (!number) {
            return std::unexpected(number.error());
        }
        if (*number != 1.0) {
            return std::unexpected(
                std::format("Nodalys simulates MOSFETs with the level 1 model only, and the model {} is level {}",
                            model.Name,
                            *number));
        }
    }
    return {};
}

} // namespace

/**
 * @brief   Resolves the parameters a diode part simulates with: its own, or its library `.model` parsed for Nodalys.
 * @param[in] circuit  Circuit that carries the library model the part names, when it names one.
 * @param[in] diode    Diode, Zener diode, or LED part to resolve the parameters of.
 * @return  The resolved parameters, or the reason its library model cannot be simulated by Nodalys.
 */
std::expected<DiodeParameters, std::string> ResolveParameters(const Circuit &circuit, const Diode &diode) {
    if (!diode.HasLibraryModel()) {
        return diode.GetParameters();
    }
    const auto model = FindModel(circuit, diode);
    if (!model) {
        return std::unexpected(model.error());
    }
    // Unset parameters keep the ngspice defaults, which DiodeParameters starts from
    DiodeParameters parameters;
    const std::map<std::string, std::string> values = ParameterList((*model)->Text);
    const auto set = [&](const char *key, double &target) -> std::expected<void, std::string> {
        const auto found = values.find(key);
        if (found == values.end()) {
            return {};
        }
        const auto number = Number(**model, key, found->second);
        if (!number) {
            return std::unexpected(number.error());
        }
        target = *number;
        return {};
    };
    for (const auto &[key, target] :
         std::vector<std::pair<const char *, double *>>{{"IS", &parameters.SaturationCurrent},
                                                        {"N", &parameters.EmissionCoefficient},
                                                        {"RS", &parameters.SeriesResistance},
                                                        {"CJO", &parameters.JunctionCapacitance},
                                                        {"VJ", &parameters.JunctionPotential},
                                                        {"M", &parameters.GradingCoefficient},
                                                        {"TT", &parameters.TransitTime},
                                                        {"BV", &parameters.BreakdownVoltage},
                                                        {"IBV", &parameters.BreakdownCurrent}}) {
        if (const auto error = set(key, *target); !error) {
            return std::unexpected(error.error());
        }
    }
    return parameters;
}

/**
 * @brief   Resolves the parameters a BJT part simulates with: its own, or its library `.model` parsed for Nodalys.
 * @param[in] circuit  Circuit that carries the library model the part names, when it names one.
 * @param[in] bjt      NPN or PNP transistor part to resolve the parameters of.
 * @return  The resolved Gummel-Poon parameters, or the reason its library model cannot be simulated by Nodalys.
 */
std::expected<BJTParameters, std::string> ResolveParameters(const Circuit &circuit, const BJT &bjt) {
    if (!bjt.HasLibraryModel()) {
        return bjt.GetParameters();
    }
    const auto model = FindModel(circuit, bjt);
    if (!model) {
        return std::unexpected(model.error());
    }
    BJTParameters parameters;
    const std::map<std::string, std::string> values = ParameterList((*model)->Text);
    for (const auto &[key, target] :
         std::vector<std::pair<const char *, double *>>{{"IS", &parameters.SaturationCurrent},
                                                        {"BF", &parameters.ForwardBeta},
                                                        {"BR", &parameters.ReverseBeta},
                                                        {"VAF", &parameters.EarlyVoltage},
                                                        {"IKF", &parameters.ForwardKneeCurrent},
                                                        {"ISE", &parameters.LeakageSaturationCurrent},
                                                        {"NE", &parameters.LeakageEmissionCoefficient},
                                                        {"RB", &parameters.BaseResistance},
                                                        {"RC", &parameters.CollectorResistance},
                                                        {"RE", &parameters.EmitterResistance},
                                                        {"CJE", &parameters.EmitterCapacitance},
                                                        {"CJC", &parameters.CollectorCapacitance},
                                                        {"TF", &parameters.TransitTime}}) {
        if (const auto found = values.find(key); found != values.end()) {
            const auto number = Number(**model, key, found->second);
            if (!number) {
                return std::unexpected(number.error());
            }
            *target = *number;
        }
    }
    return parameters;
}

/**
 * @brief   Resolves the parameters a MOSFET part simulates with: its own, or its level-1 library `.model` parsed for
 *          Nodalys.
 * @param[in] circuit  Circuit that carries the library model the part names, when it names one.
 * @param[in] mosfet   NMOS or PMOS transistor part to resolve the parameters of.
 * @return  The resolved level-1 parameters, or an error when the model is missing, malformed, or not level 1.
 */
std::expected<MOSFETParameters, std::string> ResolveParameters(const Circuit &circuit, const MOSFET &mosfet) {
    if (!mosfet.HasLibraryModel()) {
        return mosfet.GetParameters();
    }
    const auto model = FindModel(circuit, mosfet);
    if (!model) {
        return std::unexpected(model.error());
    }
    const std::map<std::string, std::string> values = ParameterList((*model)->Text);
    if (const auto level_ok = CheckMosfetLevel(**model, values); !level_ok) {
        return std::unexpected(level_ok.error());
    }
    // Without W= and L= on a library part, ngspice uses the model's size or its 100 micron instance defaults
    MOSFETParameters p{.Width = 100e-6, .Length = 100e-6};
    for (const auto &[key, target] : std::vector<std::pair<const char *, double *>>{{"VTO", &p.ThresholdVoltage},
                                                                                    {"KP", &p.Transconductance},
                                                                                    {"LAMBDA", &p.ChannelModulation},
                                                                                    {"RD", &p.DrainResistance},
                                                                                    {"RS", &p.SourceResistance},
                                                                                    {"CGSO", &p.GateSourceOverlap},
                                                                                    {"CGDO", &p.GateDrainOverlap},
                                                                                    {"W", &p.Width},
                                                                                    {"L", &p.Length}}) {
        if (const auto found = values.find(key); found != values.end()) {
            const auto number = Number(**model, key, found->second);
            if (!number) {
                return std::unexpected(number.error());
            }
            *target = *number;
        }
    }
    return p;
}

} // namespace Core
