/**
 * @file    model_parser.cpp
 * @brief   Turns the .model text of a library model into the parameter structs the native devices evaluate.
 */

#include "native/model_parser.h"

#include "model/circuit.h"
#include "model/library_model.h"
#include "spice/spice_library.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace Core::Native {

namespace {

/**
 * @brief   Splits the parameter list of a ".model" statement into upper-case keys and their value text.
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
        while (position < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[position])) != 0 || text[position] == '+')) {
            ++position;
        }
        const std::size_t key_start = position;
        while (position < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[position])) != 0 || text[position] == '_')) {
            ++position;
        }
        const std::string key = text.substr(key_start, position - key_start);
        if (key.empty() || position >= text.size() || text[position] != '=') {
            // Not a KEY=VALUE pair; skip whatever token this is
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0 &&
                   text[position] != ')') {
                ++position;
            }
            continue;
        }
        ++position;
        const std::size_t value_start = position;
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0 &&
               text[position] != ')') {
            ++position;
        }
        std::string upper = key;
        std::ranges::transform(upper, upper.begin(), [](const unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        values[upper] = text.substr(value_start, position - value_start);
    }
    return values;
}

/**
 * @brief   Reads one numeric model parameter as SPICE writes it.
 * @param[in] model  Model the parameter belongs to, for the error message.
 * @param[in] key    Upper-case parameter name.
 * @param[in] text   Value text.
 * @return  The value, or the reason the native backend cannot read it.
 */
std::expected<double, std::string> Number(const LibraryModel &model, const std::string &key, const std::string &text) {
    if (text.starts_with('{')) {
        return std::unexpected(std::format("Nodalys cannot evaluate the expression {} of the parameter {} "
                                           "in the model {}",
                                           text, key, model.Name));
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
        return std::unexpected(std::format("{} uses the model {}, which the circuit does not have", part.GetName(),
                                           part.GetLibraryModel()));
    }
    if (!CanUseLibraryModel(part.GetType(), model->Kind)) {
        return std::unexpected(
            std::format("{} cannot use the {} model {}", part.GetName(), GetModelKindName(model->Kind), model->Name));
    }
    return model;
}

} // namespace

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

std::expected<BJTParameters, std::string> ResolveParameters(const Circuit &circuit, const BJT &bjt) {
    if (!bjt.HasLibraryModel()) {
        return bjt.GetParameters();
    }
    const auto model = FindModel(circuit, bjt);
    if (!model) {
        return std::unexpected(model.error());
    }
    // Unset parameters keep the ngspice defaults, which BJTParameters starts from; junction potentials and grading
    // coefficients a vendor model sets stay at the ngspice defaults, which the native model evaluates with
    BJTParameters parameters;
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
        if (const auto error = set(key, *target); !error) {
            return std::unexpected(error.error());
        }
    }
    return parameters;
}

std::expected<MOSFETParameters, std::string> ResolveParameters(const Circuit &circuit, const MOSFET &mosfet) {
    if (!mosfet.HasLibraryModel()) {
        return mosfet.GetParameters();
    }
    const auto model = FindModel(circuit, mosfet);
    if (!model) {
        return std::unexpected(model.error());
    }
    const std::map<std::string, std::string> values = ParameterList((*model)->Text);
    // The netlist writes no W= and L= with a library model, so ngspice takes the channel size from the model or its
    // own instance defaults of 100 microns, never from the part; the native backend does the same
    MOSFETParameters parameters;
    parameters.Width = 100e-6;
    parameters.Length = 100e-6;
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
    if (const auto level = values.find("LEVEL"); level != values.end()) {
        const auto number = Number(**model, "LEVEL", level->second);
        if (!number) {
            return std::unexpected(number.error());
        }
        if (*number != 1.0) {
            return std::unexpected(std::format("Nodalys simulates MOSFETs with the level 1 model only, and "
                                               "the model {} is level {}",
                                               (*model)->Name, *number));
        }
    }
    for (const auto &[key, target] :
         std::vector<std::pair<const char *, double *>>{{"VTO", &parameters.ThresholdVoltage},
                                                        {"KP", &parameters.Transconductance},
                                                        {"LAMBDA", &parameters.ChannelModulation},
                                                        {"RD", &parameters.DrainResistance},
                                                        {"RS", &parameters.SourceResistance},
                                                        {"CGSO", &parameters.GateSourceOverlap},
                                                        {"CGDO", &parameters.GateDrainOverlap},
                                                        {"W", &parameters.Width},
                                                        {"L", &parameters.Length}}) {
        if (const auto error = set(key, *target); !error) {
            return std::unexpected(error.error());
        }
    }
    return parameters;
}

} // namespace Core::Native
