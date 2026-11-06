/**
 * @file    op_amp.cpp
 * @brief   Operational amplifiers: an ideal one with a gain-bandwidth product, and Boyle macromodels of real parts.
 * @details The macromodel follows Boyle, Cohn, Pederson and Solomon, "Macromodeling of Integrated Circuit
 *          Operational Amplifiers", IEEE JSSC SC-9, 1974, with its element values worked out from datasheet
 *          specifications as in Linear Technology's AN48 (1991). Its output limiting is the buffered one of the LTC
 *          macromodels, which limits the output without the large internal currents of the original.
 */

#include "components/op_amp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>

namespace Core {

namespace {

/// Open-loop gain of the ideal op-amp, 100 dB.
constexpr double IDEAL_GAIN = 1e5;
/// Transconductance of the gain stage of the ideal op-amp, small so the clamp current stays in the milliamperes.
constexpr double IDEAL_TRANSCONDUCTANCE = 1e-3;
/// Resistance the gain stage of the ideal op-amp drives, which sets its open-loop gain.
constexpr double IDEAL_RESISTANCE = IDEAL_GAIN / IDEAL_TRANSCONDUCTANCE;
constexpr double DEFAULT_IDEAL_BANDWIDTH = 1e6;
/// Model of the clamp diodes that stop the output of the ideal op-amp at its supply pins.
constexpr const char *IDEAL_CLAMP_MODEL_NAME = "imcsim_opamp_clamp";
/// Clamp diodes so sharp that the ideal output stops within about 10 mV of a supply pin.
constexpr const char *IDEAL_CLAMP_MODEL_PARAMETERS = "IS=1e-15 N=0.01";

// Boyle macromodel: fixed elements, as in the original paper and the LTC models
constexpr double COMPENSATION_CAPACITANCE = 30e-12;
constexpr double GAIN_STAGE_RESISTANCE = 100e3;
/// The tail resistance sets the common-mode output impedance of the input pair, from a 200 V Early voltage.
constexpr double TAIL_EARLY_VOLTAGE = 200.0;
/// kT/q at 27 degrees Celsius, the temperature ngspice simulates at.
constexpr double THERMAL_VOLTAGE = 0.025852;
constexpr double INPUT_SATURATION_CURRENT = 8e-16;
/// Resistance the output current flows through, so its voltage tells the current.
constexpr double OUTPUT_SENSE_RESISTANCE = 1.0;
/// Voltage the amplified sense voltage reaches at the short-circuit current, where it turns on the current-limit
/// diodes.
constexpr double CURRENT_LIMIT_THRESHOLD = 0.853;
/// Two limiter diodes in series drop this much at the limit, so the swing limit sources add it to the headroom.
constexpr double LIMITER_DIODE_DROPS = 1.3831;
/// Pinch-off voltage of the JFET input pair; any value works, as the pair's gain comes from its BETA.
constexpr double JFET_PINCH_OFF_VOLTAGE = -1.0;

constexpr auto INPUT_STAGES = std::to_array<OpAmpInputStage>({
    OpAmpInputStage::NPN,
    OpAmpInputStage::PNP,
    OpAmpInputStage::JFET,
});

constexpr auto MODELS = std::to_array<OpAmpModel>({
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
    // TI LM358 datasheet, SLOS068AB (October 2024), table 5.7 at 25 C: AOL 100 V/mV, GBW 0.7 MHz, SR 0.3 V/us, IB
    // 20 nA, CMRR 80 dB, ISC 40 mA, IQ 350 uA per amplifier, output 1.5 V from V+ (RL >= 2k, VS = 5 V) and 5 mV from
    // V-. It gives no phase margin or output resistance, so those keep the defaults of the uA741
    {"LM358",
     "Single-supply dual op-amp with a PNP input that takes inputs down to its negative supply, as a Boyle "
     "macromodel with TI datasheet values: 100 dB of gain, 0.7 MHz, 0.3 V/us, 20 nA of bias current, and an output "
     "1.5 V short of V+ and 5 mV short of V-.",
     "imcsim_LM358",
     false,
     {.InputStage = OpAmpInputStage::PNP,
      .OpenLoopGainDecibels = 100.0,
      .GainBandwidth = 0.7e6,
      .SlewRate = 0.3,
      .InputBiasCurrent = 20e-9,
      .CommonModeRejectionDecibels = 80.0,
      .ShortCircuitCurrent = 40e-3,
      .PositiveHeadroom = 1.5,
      .NegativeHeadroom = 5e-3,
      .SupplyCurrent = 350e-6}},
    // TI TL07xx datasheet, SLOS080W (July 2025), tables 5.8 and 5.9 for the TL07xC and TL07xI at +-15 V: AOL 200
    // V/mV, GBW 5.25 MHz, SR 20 V/us, phase margin 56 degrees, IB 65 pA, CMRR 100 dB, ZO 125 Ohm, VOM +-13.5 V into
    // 10k, IQ 1.4 mA per amplifier; the short-circuit current, 26 mA, is that of the TL07xH table
    {"TL072",
     "Low-noise dual op-amp with a JFET input of picoamperes of bias current, as a Boyle macromodel with TI "
     "datasheet values: 106 dB of gain, 5.25 MHz, 20 V/us, 65 pA of bias current, and an output 1.5 V short of each "
     "supply.",
     "imcsim_TL072",
     false,
     {.InputStage = OpAmpInputStage::JFET,
      .OpenLoopGainDecibels = 106.0,
      .GainBandwidth = 5.25e6,
      .SlewRate = 20.0,
      .PhaseMarginDegrees = 56.0,
      .InputBiasCurrent = 65e-12,
      .CommonModeRejectionDecibels = 100.0,
      .OutputResistance = 125.0,
      .ShortCircuitCurrent = 26e-3,
      .PositiveHeadroom = 1.5,
      .NegativeHeadroom = 1.5,
      .SupplyCurrent = 1.4e-3}},
});

std::string FormatNumber(const double value) {
    return std::format("{:.6g}", value);
}

/**
 * @brief   The input pair of a macromodel, its load resistors and tail current, and the model of its transistors.
 * @details The pair takes the inverting input on its left side (node 80) and, through the offset source, the
 *          non-inverting one on its right (node 90), and its tail is node 12. An NPN pair hangs from V+ with its tail
 *          current to V-; a PNP or JFET pair hangs from V- with its tail current from V+, which is what lets its
 *          inputs reach V-. Either way the input difference reaches the load resistors with a gain of exactly 1:
 *          a bipolar pair through its emitter resistors, as in AN48, a JFET pair through its BETA, since a JFET
 *          has a transconductance of 2 sqrt(BETA ID). The gate leakage of the two JFET junctions, IS each, is the
 *          bias current.
 */
std::string FormatInputStage(const OpAmpParameters &parameters, const double tail_current,
                             const double collector_resistance) {
    const double collector_current = tail_current / 2.0;
    const bool hangs_from_negative = parameters.InputStage != OpAmpInputStage::NPN;
    const char *load_node = hangs_from_negative ? "4" : "7";
    const std::string tail = hangs_from_negative ? std::format("IEE 7 12 {}\n", FormatNumber(tail_current))
                                                 : std::format("IEE 12 4 {}\n", FormatNumber(tail_current));
    const std::string loads =
        std::format("RC1 {0} 80 {1}\nRC2 {0} 90 {1}\n", load_node, FormatNumber(collector_resistance));
    if (parameters.InputStage == OpAmpInputStage::JFET) {
        const double beta = 1.0 / (4.0 * collector_current * collector_resistance * collector_resistance);
        return loads + "J1 80 2 12 JM\nJ2 90 30 12 JM\n" + tail +
               std::format(".model JM PJF(IS={} BETA={} VTO={})\n", FormatNumber(parameters.InputBiasCurrent / 2.0),
                           FormatNumber(beta), FormatNumber(JFET_PINCH_OFF_VOLTAGE));
    }
    const double beta = collector_current / parameters.InputBiasCurrent;
    // The pair amplifies its input difference by exactly 1 into the collectors
    const double emitter_resistance = beta / (beta + 1.0) * collector_resistance - THERMAL_VOLTAGE / collector_current;
    const char *polarity = hangs_from_negative ? "PNP" : "NPN";
    return loads +
           std::format("Q1 80 2 10 QM\nQ2 90 30 11 QM\nRE1 10 12 {0}\nRE2 11 12 {0}\n",
                       FormatNumber(emitter_resistance)) +
           tail +
           std::format(".model QM {}(IS={} BF={})\n", polarity, FormatNumber(INPUT_SATURATION_CURRENT),
                       FormatNumber(beta));
}

} // namespace

/**
 * @brief   Creates an ideal op-amp with a gain-bandwidth product of 1 MHz.
 */
OpAmp::OpAmp()
    : Component(ComponentType::OP_AMP, 0.0), m_Model(MODELS.data()), m_CustomParameters(MODELS.front().Parameters),
      m_IdealBandwidth(DEFAULT_IDEAL_BANDWIDTH) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> OpAmp::Clone() const {
    return std::make_unique<OpAmp>(*this);
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
 *          resistance, the headrooms and supply current are not negative and the input offset voltage is finite.
 */
bool OpAmp::IsValidParameters(const OpAmpParameters &parameters) const {
    return parameters.OpenLoopGainDecibels > 0.0 && parameters.GainBandwidth > 0.0 && parameters.SlewRate > 0.0 &&
           parameters.PhaseMarginDegrees > 0.0 && parameters.PhaseMarginDegrees < 90.0 &&
           parameters.InputBiasCurrent > 0.0 && parameters.CommonModeRejectionDecibels > 0.0 &&
           parameters.OutputResistance > OUTPUT_SENSE_RESISTANCE && parameters.ShortCircuitCurrent > 0.0 &&
           parameters.PositiveHeadroom >= 0.0 && parameters.NegativeHeadroom >= 0.0 &&
           parameters.SupplyCurrent >= 0.0 && std::isfinite(parameters.InputOffsetVoltage);
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
    return m_Model != nullptr ? m_Model->Name : CUSTOM_OP_AMP_MODEL_NAME;
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
    if (type != ComponentType::OP_AMP) {
        return {};
    }
    return MODELS;
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
 * @brief   Lists the input stages a macromodel op-amp can have.
 * @return  NPN, PNP and JFET, in the order the editor offers them.
 */
std::span<const OpAmpInputStage> GetOpAmpInputStages() {
    return INPUT_STAGES;
}

/**
 * @brief   Names an input stage, as the editor shows it and files save it.
 * @param[in] stage  Input stage to name.
 * @return  "NPN", "PNP" or "JFET".
 */
const char *GetOpAmpInputStageName(const OpAmpInputStage stage) {
    switch (stage) {
    case OpAmpInputStage::PNP:
        return "PNP";
    case OpAmpInputStage::JFET:
        return "JFET";
    case OpAmpInputStage::NPN:
    default:
        return "NPN";
    }
}

/**
 * @brief   Finds an input stage by the name GetOpAmpInputStageName() gives it.
 * @param[in] name  Name to look up, case sensitive.
 * @return  The input stage, or none for an unknown name.
 */
std::optional<OpAmpInputStage> FindOpAmpInputStage(const std::string_view name) {
    for (const OpAmpInputStage stage : INPUT_STAGES) {
        if (name == GetOpAmpInputStageName(stage)) {
            return stage;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Writes the netlist lines of an ideal op-amp.
 * @param[in] op_amp        Op-amp to write, while ideal.
 * @param[in] nodes         Nodes of its output, +, -, V+ and V- terminals.
 * @param[in] internal_node A free node number for its gain stage.
 * @return  A transconductance driving a resistor and a capacitor from the input difference, which sets the gain
 *          and its roll-off; two diodes that clamp that node to the supply pins; and a unity-gain buffer from it to
 *          the output. Element names join the kind letter, a dash and the op-amp name, such as "G-U1", so they
 *          never clash with a name the user can give. The resistor adds no noise, so the op-amp is ideal in a noise
 *          analysis too.
 * @note    ngspice converges on this where a behavioral source with a smooth limit does not: its Newton iterations
 *          start with every node at 0 V, supplies included, and a limit that depends on the supply voltages then
 *          starts saturated and swings from rail to rail. Diodes are what its solver handles best.
 */
std::string FormatIdealOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes, const int internal_node) {
    const std::string &name = op_amp.GetName();
    // The pole sits at the gain-bandwidth product over the open-loop gain
    const double capacitance = IDEAL_GAIN / (2.0 * std::numbers::pi * IDEAL_RESISTANCE * op_amp.GetIdealBandwidth());
    return std::format("G-{0} 0 {1} {2} {3} {4}\n"
                       "R-{0} {1} 0 {5} noisy=0\n"
                       "C-{0} {1} 0 {6}\n"
                       "D-{0}-P {1} {7} {8}\n"
                       "D-{0}-N {9} {1} {8}\n"
                       "E-{0} {10} 0 {1} 0 1\n",
                       name, internal_node, nodes[1], nodes[2], FormatNumber(IDEAL_TRANSCONDUCTANCE),
                       FormatNumber(IDEAL_RESISTANCE), FormatNumber(capacitance), nodes[3], IDEAL_CLAMP_MODEL_NAME,
                       nodes[4], nodes[0]);
}

/**
 * @brief   Writes the model of the clamp diodes every ideal op-amp shares.
 * @return  The ".model" line, named as FormatIdealOpAmp() refers to it.
 */
std::string FormatIdealOpAmpModel() {
    return std::format(".model {} D({})\n", IDEAL_CLAMP_MODEL_NAME, IDEAL_CLAMP_MODEL_PARAMETERS);
}

/**
 * @brief   Returns the name of the model FormatIdealOpAmpModel() writes.
 * @return  The diode model name, so the netlist writes it once however many op-amps it has.
 */
const char *GetIdealOpAmpModelName() {
    return IDEAL_CLAMP_MODEL_NAME;
}

/**
 * @brief   Writes the Boyle macromodel of an op-amp as a subcircuit.
 * @param[in] name        Subcircuit name.
 * @param[in] parameters  Datasheet specifications, checked with OpAmp::IsValidParameters().
 * @return  A ".subckt" whose pins follow the op-amp terminals (output, +, -, V+, V-), ending in ".ends".
 * @note    The input pair (FormatInputStage()), normalized to a gain of 1, sets the input bias current and, with
 *          C2, the slew rate and the gain-bandwidth product; VOS adds the input offset voltage; GA, R2, GB and RO2 the
 * open-loop gain; GCM the common-mode rejection; C1 the excess phase that gives the phase margin. Past the swing limits
 * or the short-circuit current, buffered diode limiters steal the drive of the gain stage. IP draws the rest of the
 * supply current. Values follow the equations of AN48, which reproduce its LTC 8741 listing.
 */
std::string FormatOpAmpSubcircuit(const std::string_view name, const OpAmpParameters &parameters) {
    const double tail_current = parameters.SlewRate * 1e6 * COMPENSATION_CAPACITANCE;
    const double collector_resistance =
        1.0 / (2.0 * std::numbers::pi * parameters.GainBandwidth * COMPENSATION_CAPACITANCE);
    const double excess_phase = (90.0 - parameters.PhaseMarginDegrees) * std::numbers::pi / 180.0;
    const double collector_capacitance = COMPENSATION_CAPACITANCE / 2.0 * std::tan(excess_phase);
    const double transconductance = 1.0 / collector_resistance;
    const double common_mode_transconductance =
        transconductance / std::pow(10.0, parameters.CommonModeRejectionDecibels / 20.0);
    const double gain_stage_load = parameters.OutputResistance - OUTPUT_SENSE_RESISTANCE;
    const double output_gain = std::pow(10.0, parameters.OpenLoopGainDecibels / 20.0) /
                               (transconductance * GAIN_STAGE_RESISTANCE * gain_stage_load);
    const double current_limit_gain =
        CURRENT_LIMIT_THRESHOLD / (parameters.ShortCircuitCurrent * OUTPUT_SENSE_RESISTANCE);
    const double rest_of_supply_current = std::max(parameters.SupplyCurrent - tail_current, 0.0);

    // Pins: output 6, + input 3, - input 2, V+ 7, V- 4, as in the LTC listings
    return std::format(".subckt {0} 6 3 2 7 4\n"
                       "VOS 30 3 {1}\n",
                       name, FormatNumber(parameters.InputOffsetVoltage)) +
           FormatInputStage(parameters, tail_current, collector_resistance) +
           std::format("C1 80 90 {0}\n"
                       "REE 12 0 {1}\n"
                       "GCM 0 8 12 0 {2}\nGA 8 0 80 90 {3}\n"
                       "R2 8 0 {4}\nC2 1 8 {5}\n"
                       "GB 1 0 8 0 {6}\nRO2 1 0 {7}\n"
                       "RSO 1 6 {8}\n"
                       "ECL 18 0 1 6 {9}\nGCL 0 8 20 0 1\nRCL 20 0 10\nD1 18 20 DM1\nD2 20 18 DM1\n"
                       "D3A 131 70 DM3\nD3B 13 131 DM3\nGPL 0 8 70 7 1\nVC 13 6 {10}\nRPLA 7 70 10\nRPLB 7 131 1k\n"
                       "D4A 60 141 DM3\nD4B 141 14 DM3\nGNL 0 8 60 4 1\nVE 6 14 {11}\nRNLA 60 4 10\nRNLB 141 4 1k\n"
                       "IP 7 4 {12}\n"
                       ".model DM1 D(IS=1e-20)\n"
                       ".model DM3 D(IS=1e-16)\n"
                       ".ends {13}\n",
                       FormatNumber(collector_capacitance), FormatNumber(TAIL_EARLY_VOLTAGE / tail_current),
                       FormatNumber(common_mode_transconductance), FormatNumber(transconductance),
                       FormatNumber(GAIN_STAGE_RESISTANCE), FormatNumber(COMPENSATION_CAPACITANCE),
                       FormatNumber(output_gain), FormatNumber(gain_stage_load), FormatNumber(OUTPUT_SENSE_RESISTANCE),
                       FormatNumber(current_limit_gain),
                       FormatNumber(parameters.PositiveHeadroom + LIMITER_DIODE_DROPS),
                       FormatNumber(parameters.NegativeHeadroom + LIMITER_DIODE_DROPS),
                       FormatNumber(rest_of_supply_current), name);
}

} // namespace Core
