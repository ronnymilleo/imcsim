/**
 * @file    model_cards.cpp
 * @brief   The parameters of a SPICE .model card and how they set the device parameters of the engine.
 */

#include "parser/model_cards.h"

#include <initializer_list>
#include <string_view>

namespace Nodalys {

namespace {

/**
 * @brief   Reads the parameters of a card, remembering which ones a device took.
 */
class CardReader {
public:
    CardReader(const ParsedModel &model, std::set<std::string> *read_keys) : m_Model(model), m_ReadKeys(read_keys) {}

    /// The value of a parameter set to the card, or the default when it does not give it.
    double Get(const std::string &key, const double default_value) const {
        const auto found = m_Model.Params.find(key);
        if (found == m_Model.Params.end()) {
            return default_value;
        }
        if (m_ReadKeys != nullptr) {
            m_ReadKeys->insert(key);
        }
        return found->second;
    }

    /// The value of the first of several names for one parameter that the card gives, or the default.
    double GetAny(const std::initializer_list<std::string_view> keys, const double default_value) const {
        for (const std::string_view key : keys) {
            if (m_Model.Params.contains(std::string(key))) {
                return Get(std::string(key), default_value);
            }
        }
        return default_value;
    }

private:
    const ParsedModel &m_Model;
    std::set<std::string> *m_ReadKeys;
};

} // namespace

DiodeParameters BuildDiodeParameters(const ParsedModel &model, std::set<std::string> *read_keys) {
    const CardReader card(model, read_keys);
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
    return p;
}

BjtParameters BuildBjtParameters(const ParsedModel &model, std::set<std::string> *read_keys) {
    const CardReader card(model, read_keys);
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
    return p;
}

MosfetParameters BuildMosfetParameters(const ParsedModel &model, std::set<std::string> *read_keys) {
    const CardReader card(model, read_keys);
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
    return p;
}

SwitchParameters BuildSwitchParameters(const ParsedModel &model, std::set<std::string> *read_keys) {
    const CardReader card(model, read_keys);
    SwitchParameters p;
    p.Threshold = card.GetAny({"VT", "VTHRESHOLD"}, p.Threshold);
    p.Hysteresis = card.GetAny({"VH", "VHYSTERESIS"}, p.Hysteresis);
    p.OnResistance = card.Get("RON", p.OnResistance);
    p.OffResistance = card.Get("ROFF", p.OffResistance);
    return p;
}

} // namespace Nodalys
