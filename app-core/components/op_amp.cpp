/**
 * @file    op_amp.cpp
 * @brief   Operational amplifiers: an ideal one with a gain-bandwidth product, and Boyle macromodels of real parts.
 * @details The macromodel follows Boyle, Cohn, Pederson and Solomon, "Macromodeling of Integrated Circuit
 *          Operational Amplifiers", IEEE JSSC SC-9, 1974, with its element values worked out from datasheet
 *          specifications as in Linear Technology's AN48 (1991). Its output limiting is the buffered one of the LTC
 *          macromodels, which limits the output without the large internal currents of the original.
 */

#include "op_amp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace Core {

namespace {

// Ideal op-amp: the gain stage drives this transconductance into this resistance, for an open-loop gain of 100 dB.
// The small transconductance keeps the clamp current in the milliamperes
constexpr double IdealGain = 1e5;
constexpr double IdealTransconductance = 1e-3;
constexpr double IdealResistance = IdealGain / IdealTransconductance;
constexpr double DefaultIdealBandwidth = 1e6;
// Clamp diodes so sharp that the ideal output stops within about 10 mV of a supply pin
constexpr const char *IdealClampModelName = "imcsim_opamp_clamp";
constexpr const char *IdealClampModelParameters = "IS=1e-15 N=0.01";

// Boyle macromodel: fixed elements, as in the original paper and the LTC models
constexpr double CompensationCapacitance = 30e-12;
constexpr double GainStageResistance = 100e3;
// The tail resistance sets the common-mode output impedance of the input pair, from a 200 V Early voltage
constexpr double TailEarlyVoltage = 200.0;
// kT/q at 27 degrees Celsius, the temperature ngspice simulates at
constexpr double ThermalVoltage = 0.025852;
constexpr double InputSaturationCurrent = 8e-16;
// The output current flows through this sense resistance; past the short-circuit current, the voltage across it
// amplified to this threshold turns on the current-limit diodes
constexpr double OutputSenseResistance = 1.0;
constexpr double CurrentLimitThreshold = 0.853;
// Two limiter diodes in series drop this much at the limit, so the swing limit sources add it to the headroom
constexpr double LimiterDiodeDrops = 1.3831;

constexpr auto Models = std::to_array<OpAmpModel>({
    {"Ideal",
     "No input current, 100 dB of gain rolling off at its gain-bandwidth product, and an output that "
     "reaches the supply pins.",
     "",
     true,
     {}},
    {"uA741",
     "General-purpose bipolar op-amp, as a Boyle macromodel with typical datasheet values: 106 dB of gain, "
     "1 MHz, 0.5 V/us, 80 nA of bias current, and an output 1 V short of each supply.",
     "imcsim_uA741",
     false,
     {}},
});

std::string FormatNumber(const double value) {
    return std::format("{:.6g}", value);
}

} // namespace

/**
 * @brief   Creates an ideal op-amp with a gain-bandwidth product of 1 MHz.
 */
OpAmp::OpAmp()
    : Component(ComponentType::OpAmp, 0.0), m_Model(Models.data()), m_CustomParameters(Models.front().Parameters),
      m_IdealBandwidth(DefaultIdealBandwidth) {
}

/**
 * @brief   Returns the ready model the op-amp uses.
 * @return  An entry of GetOpAmpModels(), or nullptr when it uses custom specifications.
 */
const OpAmpModel *OpAmp::GetModel() const {
    return m_Model;
}

/**
 * @brief   Makes the op-amp use a ready model.
 * @param[in] model  An entry from GetOpAmpModels(); the op-amp keeps a reference to it.
 */
void OpAmp::SetModel(const OpAmpModel &model) {
    m_Model = &model;
}

/**
 * @brief   Tells whether the op-amp is the ideal amplifier rather than a macromodel.
 * @return  True while it uses the ideal model.
 */
bool OpAmp::IsIdeal() const {
    return m_Model != nullptr && m_Model->Ideal;
}

/**
 * @brief   Returns the gain-bandwidth product the op-amp has while ideal.
 * @return  Frequency in hertz at which its open-loop gain falls to 1.
 */
double OpAmp::GetIdealBandwidth() const {
    return m_IdealBandwidth;
}

/**
 * @brief   Sets the gain-bandwidth product the op-amp has while ideal.
 * @param[in] bandwidth  Frequency in hertz; check it with IsValidIdealBandwidth() first.
 */
void OpAmp::SetIdealBandwidth(const double bandwidth) {
    m_IdealBandwidth = bandwidth;
}

/**
 * @brief   Tells whether the op-amp uses its own specifications instead of a ready model.
 * @return  True for a custom op-amp, which is always a macromodel.
 */
bool OpAmp::IsCustom() const {
    return m_Model == nullptr;
}

/**
 * @brief   Switches the op-amp to custom specifications, starting from those of its current model.
 * @note    Does nothing when it is already custom, so its specifications are kept. The ideal model starts a custom
 *          op-amp from the specifications of a uA741.
 */
void OpAmp::SetCustom() {
    if (m_Model == nullptr) {
        return;
    }
    m_CustomParameters = m_Model->Parameters;
    m_Model = nullptr;
}

/**
 * @brief   Makes the op-amp custom with the given specifications.
 * @param[in] parameters  New specifications; check them with IsValidParameters() first.
 */
void OpAmp::SetCustomParameters(const OpAmpParameters &parameters) {
    m_CustomParameters = parameters;
    m_Model = nullptr;
}

/**
 * @brief   Checks whether custom specifications can be turned into a macromodel.
 * @param[in] parameters  Candidate specifications.
 * @return  True when the gains, the gain-bandwidth product, the slew rate, the bias and short-circuit currents are
 *          positive, the phase margin is between 0 and 90 degrees, the output resistance is above the 1 Ohm sense
 *          resistance and the headrooms and supply current are not negative.
 */
bool OpAmp::IsValidParameters(const OpAmpParameters &parameters) const {
    return parameters.OpenLoopGainDecibels > 0.0 && parameters.GainBandwidth > 0.0 && parameters.SlewRate > 0.0 &&
           parameters.PhaseMarginDegrees > 0.0 && parameters.PhaseMarginDegrees < 90.0 &&
           parameters.InputBiasCurrent > 0.0 && parameters.CommonModeRejectionDecibels > 0.0 &&
           parameters.OutputResistance > OutputSenseResistance && parameters.ShortCircuitCurrent > 0.0 &&
           parameters.PositiveHeadroom >= 0.0 && parameters.NegativeHeadroom >= 0.0 && parameters.SupplyCurrent >= 0.0;
}

/**
 * @brief   Returns the specifications the op-amp is simulated with as a macromodel.
 * @return  Those of its ready model, or its custom ones; an ideal op-amp ignores them.
 */
const OpAmpParameters &OpAmp::GetParameters() const {
    return m_Model != nullptr ? m_Model->Parameters : m_CustomParameters;
}

/**
 * @brief   Returns the name of the model, as shown in the editor and saved in files.
 * @return  "Ideal", a ready model name such as "uA741", or CustomOpAmpModelName.
 */
const char *OpAmp::GetModelName() const {
    return m_Model != nullptr ? m_Model->Name : CustomOpAmpModelName;
}

/**
 * @brief   Returns the name of the subcircuit a macromodel op-amp uses in a netlist.
 * @return  The ready model subcircuit, or "imcsim_opamp_" followed by the op-amp name, since custom
 *          specifications belong to one op-amp; empty for the ideal model, which needs no subcircuit.
 */
std::string OpAmp::GetSpiceModelName() const {
    if (m_Model != nullptr) {
        return m_Model->SpiceName;
    }
    return std::format("imcsim_opamp_{}", GetName());
}

/**
 * @brief   Lists the ready op-amp models.
 * @param[in] type  Kind of component; only ComponentType::OpAmp has models.
 * @return  The ideal amplifier first, then the real parts; empty for other kinds.
 */
std::span<const OpAmpModel> GetOpAmpModels(const ComponentType type) {
    if (type != ComponentType::OpAmp) {
        return {};
    }
    return Models;
}

/**
 * @brief   Finds a ready op-amp model by name.
 * @param[in] type  Kind of component.
 * @param[in] name  Model name as saved in files, such as "uA741".
 * @return  The model, or nullptr when the kind has no model of that name.
 */
const OpAmpModel *FindOpAmpModel(const ComponentType type, const std::string_view name) {
    for (const OpAmpModel &model : GetOpAmpModels(type)) {
        if (name == model.Name) {
            return &model;
        }
    }
    return nullptr;
}

/**
 * @brief   Checks whether a gain-bandwidth product suits the ideal op-amp.
 * @param[in] bandwidth  Candidate frequency in hertz.
 * @return  True when it is positive.
 */
bool IsValidIdealBandwidth(const double bandwidth) {
    return bandwidth > 0.0;
}

/**
 * @brief   Writes the netlist lines of an ideal op-amp.
 * @param[in] op_amp        Op-amp to write, while ideal.
 * @param[in] nodes         Nodes of its output, +, -, V+ and V- terminals.
 * @param[in] internal_node A free node number for its gain stage.
 * @return  A transconductance driving a resistor and a capacitor from the input difference, which sets the gain
 *          and its roll-off; two diodes that clamp that node to the supply pins; and a unity-gain buffer from it to
 *          the output. Element names join the kind letter, a dash and the op-amp name, such as "G-U1", so they
 *          never clash with a name the user can give.
 * @note    ngspice converges on this where a behavioral source with a smooth limit does not: its Newton iterations
 *          start with every node at 0 V, supplies included, and a limit that depends on the supply voltages then
 *          starts saturated and swings from rail to rail. Diodes are what its solver handles best.
 */
std::string FormatIdealOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes, const int internal_node) {
    const std::string &name = op_amp.GetName();
    // The pole sits at the gain-bandwidth product over the open-loop gain
    const double capacitance = IdealGain / (2.0 * std::numbers::pi * IdealResistance * op_amp.GetIdealBandwidth());
    return std::format("G-{0} 0 {1} {2} {3} {4}\n"
                       "R-{0} {1} 0 {5}\n"
                       "C-{0} {1} 0 {6}\n"
                       "D-{0}-P {1} {7} {8}\n"
                       "D-{0}-N {9} {1} {8}\n"
                       "E-{0} {10} 0 {1} 0 1\n",
                       name, internal_node, nodes[1], nodes[2], FormatNumber(IdealTransconductance),
                       FormatNumber(IdealResistance), FormatNumber(capacitance), nodes[3], IdealClampModelName,
                       nodes[4], nodes[0]);
}

/**
 * @brief   Writes the model of the clamp diodes every ideal op-amp shares.
 * @return  The ".model" line, named as FormatIdealOpAmp() refers to it.
 */
std::string FormatIdealOpAmpModel() {
    return std::format(".model {} D({})\n", IdealClampModelName, IdealClampModelParameters);
}

/**
 * @brief   Returns the name of the model FormatIdealOpAmpModel() writes.
 * @return  The diode model name, so the netlist writes it once however many op-amps it has.
 */
const char *GetIdealOpAmpModelName() {
    return IdealClampModelName;
}

/**
 * @brief   Writes the Boyle macromodel of an op-amp as a subcircuit.
 * @param[in] name        Subcircuit name.
 * @param[in] parameters  Datasheet specifications, checked with OpAmp::IsValidParameters().
 * @return  A ".subckt" whose pins follow the op-amp terminals (output, +, -, V+, V-), ending in ".ends".
 * @note    An NPN pair with a unit-gain normalized input stage sets the input bias current and, with C2, the slew
 *          rate and the gain-bandwidth product; GA, R2, GB and RO2 the open-loop gain; GCM the common-mode
 *          rejection; C1 the excess phase that gives the phase margin. Past the swing limits or the short-circuit
 *          current, buffered diode limiters steal the drive of the gain stage. IP draws the rest of the supply
 *          current. Values follow the equations of AN48, which reproduce its LTC 8741 listing.
 */
std::string FormatOpAmpSubcircuit(const std::string_view name, const OpAmpParameters &parameters) {
    const double tail_current = parameters.SlewRate * 1e6 * CompensationCapacitance;
    const double collector_current = tail_current / 2.0;
    const double beta = collector_current / parameters.InputBiasCurrent;
    const double collector_resistance =
        1.0 / (2.0 * std::numbers::pi * parameters.GainBandwidth * CompensationCapacitance);
    // The pair amplifies its input difference by exactly 1 into the collectors
    const double emitter_resistance = beta / (beta + 1.0) * collector_resistance - ThermalVoltage / collector_current;
    const double excess_phase = (90.0 - parameters.PhaseMarginDegrees) * std::numbers::pi / 180.0;
    const double collector_capacitance = CompensationCapacitance / 2.0 * std::tan(excess_phase);
    const double transconductance = 1.0 / collector_resistance;
    const double common_mode_transconductance =
        transconductance / std::pow(10.0, parameters.CommonModeRejectionDecibels / 20.0);
    const double gain_stage_load = parameters.OutputResistance - OutputSenseResistance;
    const double output_gain = std::pow(10.0, parameters.OpenLoopGainDecibels / 20.0) /
                               (transconductance * GainStageResistance * gain_stage_load);
    const double current_limit_gain = CurrentLimitThreshold / (parameters.ShortCircuitCurrent * OutputSenseResistance);
    const double rest_of_supply_current = std::max(parameters.SupplyCurrent - tail_current, 0.0);

    // Pins: output 6, + input 3, - input 2, V+ 7, V- 4, as in the LTC listings
    return std::format(".subckt {0} 6 3 2 7 4\n"
                       "RC1 7 80 {1}\nRC2 7 90 {1}\n"
                       "Q1 80 2 10 QM\nQ2 90 3 11 QM\n"
                       "C1 80 90 {2}\n"
                       "RE1 10 12 {3}\nRE2 11 12 {3}\n"
                       "IEE 12 4 {4}\nREE 12 0 {5}\n"
                       "GCM 0 8 12 0 {6}\nGA 8 0 80 90 {7}\n"
                       "R2 8 0 {8}\nC2 1 8 {9}\n"
                       "GB 1 0 8 0 {10}\nRO2 1 0 {11}\n"
                       "RSO 1 6 {12}\n"
                       "ECL 18 0 1 6 {13}\nGCL 0 8 20 0 1\nRCL 20 0 10\nD1 18 20 DM1\nD2 20 18 DM1\n"
                       "D3A 131 70 DM3\nD3B 13 131 DM3\nGPL 0 8 70 7 1\nVC 13 6 {14}\nRPLA 7 70 10\nRPLB 7 131 1k\n"
                       "D4A 60 141 DM3\nD4B 141 14 DM3\nGNL 0 8 60 4 1\nVE 6 14 {15}\nRNLA 60 4 10\nRNLB 141 4 1k\n"
                       "IP 7 4 {16}\n"
                       ".model QM NPN(IS={17} BF={18})\n"
                       ".model DM1 D(IS=1e-20)\n"
                       ".model DM3 D(IS=1e-16)\n"
                       ".ends {0}\n",
                       name, FormatNumber(collector_resistance), FormatNumber(collector_capacitance),
                       FormatNumber(emitter_resistance), FormatNumber(tail_current),
                       FormatNumber(TailEarlyVoltage / tail_current), FormatNumber(common_mode_transconductance),
                       FormatNumber(transconductance), FormatNumber(GainStageResistance),
                       FormatNumber(CompensationCapacitance), FormatNumber(output_gain), FormatNumber(gain_stage_load),
                       FormatNumber(OutputSenseResistance), FormatNumber(current_limit_gain),
                       FormatNumber(parameters.PositiveHeadroom + LimiterDiodeDrops),
                       FormatNumber(parameters.NegativeHeadroom + LimiterDiodeDrops),
                       FormatNumber(rest_of_supply_current), FormatNumber(InputSaturationCurrent), FormatNumber(beta));
}

} // namespace Core
