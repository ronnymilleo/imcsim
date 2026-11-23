/**
 * @file    model_cards.cpp
 * @brief   The parameters of a SPICE .model card and how they set the device parameters of the engine.
 */

#include "parser/syntax/model_cards.h"

#include "parser/syntax/spice_text.h"
#include "text/text_case.h"

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

/**
 * @brief   Reads the parameters of a card, remembering which ones a device took.
 */
class CardReader {
public:
    explicit CardReader(const ParsedModel &model) : m_Model(model) {}

    /// The value of a parameter set to the card, or the default when it does not give it.
    double Get(const std::string &key, const double default_value) {
        const auto found = m_Model.Params.find(key);
        if (found == m_Model.Params.end()) {
            return default_value;
        }
        m_ReadKeys.insert(key);
        return found->second;
    }

    /// The value of the first of several names for one parameter that the card gives, or the default.
    double GetAny(const std::initializer_list<std::string_view> keys, const double default_value) {
        for (const std::string_view key : keys) {
            if (m_Model.Params.contains(std::string(key))) {
                return Get(std::string(key), default_value);
            }
        }
        return default_value;
    }

    /// The parameters built so far with the names the device read, which leaves the reader without them.
    template <typename T>
    BuiltParameters<T> Finish(T parameters) {
        return {.Parameters = std::move(parameters), .ReadKeys = std::move(m_ReadKeys)};
    }

private:
    const ParsedModel &m_Model;
    std::set<std::string> m_ReadKeys;
};

/**
 * @brief   The text of the parameters of a card: what follows the type, without the parentheses around the list.
 */
std::string_view FindParameterText(const std::string_view after_name, std::string &type) {
    const std::string_view text = TrimSpaces(after_name);
    const std::size_t type_end = text.find_first_of(" \t(");
    type = ToUpper(text.substr(0, type_end));
    std::string_view parameters = type_end == std::string_view::npos ? std::string_view() : text.substr(type_end);
    parameters = TrimSpaces(parameters);
    if (parameters.size() >= 2 && parameters.front() == '(' && parameters.back() == ')') {
        parameters = parameters.substr(1, parameters.size() - 2);
    }
    return parameters;
}

} // namespace

/**
 * @brief   Splits a .model card into its name, its type and its parameter words, the same way for a netlist and for a
 *          subcircuit kept as text.
 * @param[in] tokens  Tokens of the card line, from the .model word on.
 * @return  The words of the card, or none when it has no name and type.
 */
std::optional<ModelCardWords> SplitModelCard(const std::vector<std::string> &tokens) {
    constexpr std::size_t FIRST_AFTER_NAME = 2;
    if (tokens.size() <= FIRST_AFTER_NAME) {
        return std::nullopt;
    }
    std::string rest;
    for (std::size_t index = FIRST_AFTER_NAME; index < tokens.size(); ++index) {
        rest += index == FIRST_AFTER_NAME ? "" : " ";
        rest += tokens[index];
    }
    ModelCardWords card{.Name = tokens[1], .TypeName = {}, .Assignments = {}};
    card.Assignments = SplitTokens(FindParameterText(rest, card.TypeName));
    return card;
}

/**
 * @brief   The kind of device a .model type stands for.
 * @param[in] type  Type in upper case, such as "NPN".
 * @return  The kind, or none for a type the engine has no device for.
 */
std::optional<ParsedModel::ModelKind> FindModelKind(const std::string_view type) {
    if (type == "D") {
        return ParsedModel::ModelKind::DIODE;
    }
    if (type == "NPN") {
        return ParsedModel::ModelKind::BJT_NPN;
    }
    if (type == "PNP") {
        return ParsedModel::ModelKind::BJT_PNP;
    }
    if (type == "NMOS") {
        return ParsedModel::ModelKind::MOSFET_NMOS;
    }
    if (type == "PMOS") {
        return ParsedModel::ModelKind::MOSFET_PMOS;
    }
    if (type == "SW" || type == "VSWITCH") {
        return ParsedModel::ModelKind::SWITCH;
    }
    return std::nullopt;
}

/**
 * @brief   The diode parameters a card gives, the others at the defaults of the device.
 */
BuiltParameters<DiodeParameters> BuildDiodeParameters(const ParsedModel &model) {
    CardReader card(model);
    DiodeParameters p;
    p.SaturationCurrent = card.Get("IS", p.SaturationCurrent);
    p.SeriesResistance = card.Get("RS", p.SeriesResistance);
    p.EmissionCoefficient = card.Get("N", p.EmissionCoefficient);
    p.BreakdownVoltage = card.Get("BV", p.BreakdownVoltage);
    p.BreakdownCurrent = card.Get("IBV", p.BreakdownCurrent);
    p.JunctionCapacitance = card.Get("CJO", p.JunctionCapacitance);
    p.JunctionPotential = card.Get("VJ", p.JunctionPotential);
    p.GradingCoefficient = card.Get("M", p.GradingCoefficient);
    p.TransitTime = card.Get("TT", p.TransitTime);
    p.EnergyGap = card.Get("EG", p.EnergyGap);
    p.SaturationCurrentExponent = card.Get("XTI", p.SaturationCurrentExponent);
    return card.Finish(p);
}

/**
 * @brief   The bipolar transistor parameters a card gives, the others at the defaults of the device.
 */
BuiltParameters<BjtParameters> BuildBjtParameters(const ParsedModel &model) {
    CardReader card(model);
    BjtParameters p;
    p.SaturationCurrent = card.Get("IS", p.SaturationCurrent);
    p.ForwardBeta = card.Get("BF", p.ForwardBeta);
    p.ReverseBeta = card.Get("BR", p.ReverseBeta);
    p.EarlyVoltage = card.Get("VAF", p.EarlyVoltage);
    p.ForwardKneeCurrent = card.Get("IKF", p.ForwardKneeCurrent);
    p.LeakageSaturationCurrent = card.Get("ISE", p.LeakageSaturationCurrent);
    p.LeakageEmissionCoefficient = card.Get("NE", p.LeakageEmissionCoefficient);
    p.BaseResistance = card.Get("RB", p.BaseResistance);
    p.CollectorResistance = card.Get("RC", p.CollectorResistance);
    p.EmitterResistance = card.Get("RE", p.EmitterResistance);
    p.EmitterCapacitance = card.Get("CJE", p.EmitterCapacitance);
    p.CollectorCapacitance = card.Get("CJC", p.CollectorCapacitance);
    p.TransitTime = card.Get("TF", p.TransitTime);
    p.EnergyGap = card.Get("EG", p.EnergyGap);
    p.SaturationCurrentExponent = card.Get("XTI", p.SaturationCurrentExponent);
    p.BetaExponent = card.Get("XTB", p.BetaExponent);
    return card.Finish(p);
}

/**
 * @brief   The MOSFET parameters a card gives, the others at the defaults of the device.
 */
BuiltParameters<MosfetParameters> BuildMosfetParameters(const ParsedModel &model) {
    CardReader card(model);
    MosfetParameters p;
    p.ThresholdVoltage = card.Get("VTO", p.ThresholdVoltage);
    p.Transconductance = card.Get("KP", p.Transconductance);
    p.ChannelModulation = card.Get("LAMBDA", p.ChannelModulation);
    p.DrainResistance = card.Get("RD", p.DrainResistance);
    p.SourceResistance = card.Get("RS", p.SourceResistance);
    p.GateSourceOverlap = card.Get("CGSO", p.GateSourceOverlap);
    p.GateDrainOverlap = card.Get("CGDO", p.GateDrainOverlap);
    p.Length = card.Get("L", p.Length);
    p.Width = card.Get("W", p.Width);
    p.SurfacePotential = card.Get("PHI", p.SurfacePotential);
    return card.Finish(p);
}

/**
 * @brief   The switch parameters a card gives, the others at the defaults of the device.
 */
BuiltParameters<SwitchParameters> BuildSwitchParameters(const ParsedModel &model) {
    CardReader card(model);
    SwitchParameters p;
    p.Threshold = card.GetAny({"VT", "VTHRESHOLD"}, p.Threshold);
    p.Hysteresis = card.GetAny({"VH", "VHYSTERESIS"}, p.Hysteresis);
    p.OnResistance = card.Get("RON", p.OnResistance);
    p.OffResistance = card.Get("ROFF", p.OffResistance);
    return card.Finish(p);
}

/**
 * @brief   Lists the parameters of a card that no device of the engine reads, so a caller can say so.
 * @param[in] model      The card.
 * @param[in] read_keys  Parameters the device took from it.
 * @return  Their upper-case names separated by commas, such as "IKR, VAR", or empty when the device read them all.
 *          LEVEL is never listed: it names the kind of model, not a value the device takes.
 */
std::string ListIgnoredParameters(const ParsedModel &model, const std::set<std::string> &read_keys) {
    std::string ignored;
    for (const auto &[key, value] : model.Params) {
        if (key != "LEVEL" && !read_keys.contains(key)) {
            ignored += ignored.empty() ? key : ", " + key;
        }
    }
    return ignored;
}

} // namespace Nodalys
