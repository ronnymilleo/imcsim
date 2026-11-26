/**
 * @file    igbt_device.cpp
 * @brief   IGBT of Nodalys: the conduction of a MOS-gated bipolar switch from its datasheet figures, with the tail
 *          current of its turn-off.
 */

#include "devices/semiconductors/igbt_device.h"

#include "devices/stamp_helpers.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace Nodalys {

namespace {

/// Width of the knee where the on-state line starts, in Volts: the line rises from zero below V0 to its slope 1/RCE
/// over about this span, as the collector junction of the bipolar part opens.
constexpr double ON_KNEE_VOLTS = 0.05;
/// Width over which the channel opens around the gate threshold, in Volts, so the channel current and its slope are
/// continuous there.
constexpr double GATE_KNEE_VOLTS = 0.05;
/// Exponent of the smooth minimum of the channel and on-state currents: the larger, the sharper the corner between
/// the saturated channel and the on-state line.
constexpr double BLEND_EXPONENT = 4.0;
/// Width over which the tail current fades as the collector falls to the emitter voltage, in Volts: the stored
/// charge only flows out of a forward-biased device.
constexpr double TAIL_KNEE_VOLTS = 0.1;
// Limits of a Newton step of the collector voltage above V0, those SPICE puts on the drain voltage of a MOSFET
/// Above it, a rising voltage may at most triple; a falling one stops at FALL_FLOOR_VOLTS.
constexpr double HIGH_BIAS_VOLTS = 3.5;
constexpr double FALL_FLOOR_VOLTS = 2.0;
/// Below HIGH_BIAS_VOLTS, a rise stops at the ceiling, and a fall that starts above the floor stops there; below
/// the floor the line has faded into the leakage.
constexpr double LOW_BIAS_CEILING_VOLTS = 4.0;
constexpr double LOW_BIAS_FLOOR_VOLTS = -0.5;
/// Span above the knee where the on-state line still curves, in Volts; a step that starts there is limited like the
/// voltage of a junction, since the softplus below the knee is an exponential.
constexpr double CURVED_SPAN_VOLTS = 5.0 * ON_KNEE_VOLTS;
/// Gate-emitter leakage, the IGES of a datasheet, which also keeps an undriven gate from floating the matrix.
constexpr double GATE_LEAKAGE_CONDUCTANCE = 1e-9;

/**
 * @brief   \f$\ln(1 + e^u)\f$ without overflow for a large argument.
 */
double Softplus(const double argument) {
    return argument > 0.0 ? argument + std::log1p(std::exp(-argument)) : std::log1p(std::exp(argument));
}

/**
 * @brief   \f$1 / (1 + e^{-u})\f$, the slope of Softplus().
 */
double Sigmoid(const double argument) {
    return argument >= 0.0 ? 1.0 / (1.0 + std::exp(-argument)) : std::exp(argument) / (1.0 + std::exp(argument));
}

/**
 * @brief   Limits a large Newton swing of a voltage measured from the knee of the on-state line, as SPICE limits the
 *          drain-source voltage of a MOSFET (DEVlimvds).
 * @param[in] voltage_new  Candidate voltage of this iteration.
 * @param[in] voltage_old  Voltage of the previous iteration.
 * @return  The candidate, held within reach of the previous voltage.
 */
double LimitLargeSwing(const double voltage_new, const double voltage_old) {
    if (voltage_old >= HIGH_BIAS_VOLTS) {
        return voltage_new > voltage_old ? std::min(voltage_new, 3.0 * voltage_old + 2.0)
                                         : std::max(voltage_new, FALL_FLOOR_VOLTS);
    }
    if (voltage_new > voltage_old) {
        return std::min(voltage_new, LOW_BIAS_CEILING_VOLTS);
    }
    // Far below the knee the device only leaks, which Newton solves exactly, so a reverse voltage is free to fall
    return voltage_old > LOW_BIAS_FLOOR_VOLTS ? std::max(voltage_new, LOW_BIAS_FLOOR_VOLTS) : voltage_new;
}

/**
 * @brief   Limits a step inside the knee of the on-state line logarithmically, as SPICE limits a junction (pnjlim).
 * @details Below the knee the line is a softplus, an exponential: a linearization there throws the voltage far past
 *          the knee and the next one throws it back below, all the more when the saturated channel holds the current
 *          and leaves the linearization almost flat. A step that starts in the curve moves by the log of its size, up
 *          or down; far below the knee the device only leaks, which Newton solves exactly.
 * @param[in] voltage_new  Candidate voltage above the knee of this iteration.
 * @param[in] voltage_old  Voltage above the knee of the previous iteration.
 * @return  The candidate, limited when it leaves the curve.
 */
double LimitInKnee(const double voltage_new, const double voltage_old) {
    const double step = voltage_new - voltage_old;
    if (std::fabs(step) <= 2.0 * ON_KNEE_VOLTS || voltage_old >= CURVED_SPAN_VOLTS) {
        return voltage_new;
    }
    if (step < 0.0) {
        return voltage_old > LOW_BIAS_FLOOR_VOLTS ? voltage_old - ON_KNEE_VOLTS * std::log1p(-step / ON_KNEE_VOLTS)
                                                  : voltage_new;
    }
    if (voltage_old > 0.0) {
        return voltage_old + ON_KNEE_VOLTS * std::log1p(step / ON_KNEE_VOLTS);
    }
    return voltage_new > ON_KNEE_VOLTS ? ON_KNEE_VOLTS * std::log(voltage_new / ON_KNEE_VOLTS) : voltage_new;
}

/**
 * @brief   Limits the Newton step of a voltage measured from the knee of the on-state line: large swings as SPICE
 *          limits the drain-source voltage of a MOSFET (DEVlimvds), and a step in the knee as a junction.
 * @param[in] voltage_new  Candidate voltage above the knee of this iteration.
 * @param[in] voltage_old  Voltage above the knee of the previous iteration.
 * @return  The candidate, held within reach of the previous voltage.
 */
double LimitAboveKnee(const double voltage_new, const double voltage_old) {
    return LimitInKnee(LimitLargeSwing(voltage_new, voltage_old), voltage_old);
}

} // namespace

/**
 * @brief   Constructs an IGBT between its collector, gate and emitter.
 * @param[in] collector   Collector MNA node index.
 * @param[in] gate        Gate MNA node index.
 * @param[in] emitter     Emitter MNA node index.
 * @param[in] parameters  Datasheet figures; the capacitances and the diode are added by the compiler around it.
 */
IgbtDevice::IgbtDevice(const int collector, const int gate, const int emitter, IgbtParameters parameters)
    : m_Collector(collector), m_Gate(gate), m_Emitter(emitter), m_Parameters(std::move(parameters)),
      m_ChannelGain(m_Parameters.Transconductance * m_Parameters.Transconductance
                    / (2.0 * m_Parameters.TransconductanceCurrent)) {
}

/**
 * @brief   Stamps the collector current linearized at the iterate, and the gate leakage.
 * @details In a transient step the tail decays from the stored current of the last accepted point over the step;
 *          every other solve has no tail.
 */
void IgbtDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    m_StepStart = m_StoredCurrent;
    const bool is_step = time.IsTransient && !time.IsTransientStart && time.TimeStep > 0.0;
    m_StepDecay = is_step ? std::exp(-time.TimeStep / m_Parameters.TailTimeConstant) : 0.0;

    const double vce = BranchVoltage(x, m_Collector, m_Emitter);
    const double vge = BranchVoltage(x, m_Gate, m_Emitter);
    const Conduction conduction = Evaluate(x);
    const double equivalent = conduction.Current - conduction.OutputSlope * vce - conduction.Transconductance * vge;
    StampConductance(sys, m_Collector, m_Emitter, conduction.OutputSlope);
    sys.AddA(m_Collector, m_Gate, conduction.Transconductance);
    sys.AddA(m_Collector, m_Emitter, -conduction.Transconductance);
    sys.AddA(m_Emitter, m_Gate, -conduction.Transconductance);
    sys.AddA(m_Emitter, m_Emitter, conduction.Transconductance);
    StampCurrentSource(sys, m_Collector, m_Emitter, equivalent);
    StampConductance(sys, m_Gate, m_Emitter, GATE_LEAKAGE_CONDUCTANCE);
}

/**
 * @brief   The current entering a terminal: the collector current into the collector, the gate leakage into the
 *          gate, and both out of the emitter.
 */
double IgbtDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double collector = Evaluate(x).Current;
    const double gate = GATE_LEAKAGE_CONDUCTANCE * BranchVoltage(x, m_Gate, m_Emitter);
    switch (terminal) {
    case 0:
        return collector;
    case 1:
        return gate;
    case 2:
    default:
        return -collector - gate;
    }
}

/**
 * @brief   The small-signal current entering a terminal, from the slopes of the collector current at the operating
 *          point.
 */
std::complex<double> IgbtDevice::AcTerminalCurrent(const int terminal,
                                                   const std::vector<double> &x_dc,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const Conduction conduction = Evaluate(x_dc);
    const std::complex<double> vce = AcBranchVoltage(x_ac, m_Collector, m_Emitter);
    const std::complex<double> vge = AcBranchVoltage(x_ac, m_Gate, m_Emitter);
    const std::complex<double> collector = conduction.OutputSlope * vce + conduction.Transconductance * vge;
    const std::complex<double> gate = GATE_LEAKAGE_CONDUCTANCE * vge;
    switch (terminal) {
    case 0:
        return collector;
    case 1:
        return gate;
    case 2:
    default:
        return -collector - gate;
    }
}

/**
 * @brief   Keeps the collector voltage from swinging across the on-state knee in one Newton step.
 * @details Between a blocking iterate, where the saturated channel carries a large current, and the conducting one, the
 *          linearized line throws the collector far below the emitter and back; the limit lets it approach the knee
 *          instead.
 * @param[in,out] x_new  Candidate solution of this iteration.
 * @param[in]     x_old  Solution of the previous iteration.
 * @return  True when x_new changed.
 */
bool IgbtDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    const double knee = m_Parameters.OnThresholdVoltage;
    const double above_new = BranchVoltage(x_new, m_Collector, m_Emitter) - knee;
    const double above_old = BranchVoltage(x_old, m_Collector, m_Emitter) - knee;
    // Compared above the knee, where the limit hands the candidate back unchanged, so rounding never reads as a limit
    const double limited = LimitAboveKnee(above_new, above_old);
    if (limited == above_new) {
        return false;
    }
    const double delta = limited - above_new;
    if (m_Collector >= 0) {
        x_new[static_cast<std::size_t>(m_Collector)] += delta;
    } else if (m_Emitter >= 0) {
        x_new[static_cast<std::size_t>(m_Emitter)] -= delta;
    }
    return true;
}

/**
 * @brief   Moves the stored current to the accepted point: up with the conduction at once, down toward it with the
 *          tail decay of the step.
 */
void IgbtDevice::AcceptStep(const std::vector<double> &x) {
    const double conduction =
        EvaluateStatic(BranchVoltage(x, m_Collector, m_Emitter), BranchVoltage(x, m_Gate, m_Emitter)).Current;
    const bool is_decaying = m_StepDecay > 0.0 && conduction < m_StepStart;
    m_StoredCurrent = is_decaying ? conduction + (m_StepStart - conduction) * m_StepDecay : conduction;
}

std::unique_ptr<Device> IgbtDevice::Clone() const {
    return std::make_unique<IgbtDevice>(*this);
}

/**
 * @brief   The conduction of the channel and the bipolar part, without the tail and the leakage.
 * @details The channel saturates at \f$K/2\,v_{ov}^2\f$, the overdrive \f$v_{ov}\f$ being the gate voltage above
 *          the threshold through a softplus; the on-state line carries \f$(V_{CE} - V_0)/R_{CE}\f$ through a softplus
 *          too. The current is their smooth minimum \f$ab / (a^m + b^m)^{1/m}\f$, which follows the smaller one and
 *          is zero when either is.
 * @param[in] vce  Collector-emitter voltage, in Volts.
 * @param[in] vge  Gate-emitter voltage, in Volts.
 * @return  Current from the collector to the emitter and its slopes.
 */
IgbtDevice::Conduction IgbtDevice::EvaluateStatic(const double vce, const double vge) const {
    const double line_argument = (vce - m_Parameters.OnThresholdVoltage) / ON_KNEE_VOLTS;
    const double line = ON_KNEE_VOLTS * Softplus(line_argument) / m_Parameters.OnResistance;
    const double line_slope = Sigmoid(line_argument) / m_Parameters.OnResistance;

    const double gate_argument = (vge - m_Parameters.GateThresholdVoltage) / GATE_KNEE_VOLTS;
    const double overdrive = GATE_KNEE_VOLTS * Softplus(gate_argument);
    const double channel = 0.5 * m_ChannelGain * overdrive * overdrive;
    const double channel_slope = m_ChannelGain * overdrive * Sigmoid(gate_argument);

    const double line_power = std::pow(line, BLEND_EXPONENT);
    const double channel_power = std::pow(channel, BLEND_EXPONENT);
    const double sum = line_power + channel_power;
    if (sum <= DBL_MIN) {
        return {.Current = 0.0, .OutputSlope = 0.0, .Transconductance = 0.0};
    }
    const double scale = std::pow(sum, -1.0 / BLEND_EXPONENT);
    const double current = line * channel * scale;
    return {.Current = current,
            .OutputSlope = channel * scale * channel_power / sum * line_slope,
            .Transconductance = line * scale * line_power / sum * channel_slope};
}

/**
 * @brief   The collector current at an iterate: the conduction, the tail of the step being solved and the leakage.
 * @details The tail is a share of the stored current above the conduction, decayed over the step, and fades as the
 *          collector falls to the emitter voltage.
 * @param[in] x  Iterate or solution.
 * @return  Current from the collector to the emitter and its slopes.
 */
IgbtDevice::Conduction IgbtDevice::Evaluate(const std::vector<double> &x) const {
    const double vce = BranchVoltage(x, m_Collector, m_Emitter);
    const double vge = BranchVoltage(x, m_Gate, m_Emitter);
    Conduction conduction = EvaluateStatic(vce, vge);
    const double excess = m_StepStart - conduction.Current;
    if (m_StepDecay > 0.0 && excess > 0.0) {
        const double share = m_Parameters.TailFraction * m_StepDecay;
        const double fade = std::tanh(vce / TAIL_KNEE_VOLTS);
        const double forward = 0.5 * (1.0 + fade);
        const double forward_slope = 0.5 * (1.0 - fade * fade) / TAIL_KNEE_VOLTS;
        const double tail = share * excess * forward;
        conduction.OutputSlope += share * (excess * forward_slope - conduction.OutputSlope * forward);
        conduction.Transconductance -= share * conduction.Transconductance * forward;
        conduction.Current += tail;
    }
    conduction.Current += vce / m_Parameters.OffResistance;
    conduction.OutputSlope += 1.0 / m_Parameters.OffResistance;
    return conduction;
}

} // namespace Nodalys
