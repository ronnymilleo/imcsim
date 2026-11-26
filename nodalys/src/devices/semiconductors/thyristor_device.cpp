/**
 * @file    thyristor_device.cpp
 * @brief   Thyristor (SCR), TRIAC and GTO of Nodalys: a latching switch fired by gate current and held on by its main
 *          current.
 */

#include "devices/semiconductors/thyristor_device.h"

#include "devices/stamp_helpers.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Nodalys {

namespace {

/// Gate-cathode leakage that keeps the gate node tied to the cathode when the junction is below its threshold, so a
/// gate left undriven never floats the matrix.
constexpr double GATE_OFF_CONDUCTANCE = 1e-9;
/// Closest a predicted commutation may lie ahead of the time for the transient to land on it; nearer, the step passes
/// it and the error is that small a share of a period.
constexpr double MIN_BREAKPOINT_GAP = 1e-12;

/// Sign of a quantity, zero at zero, which mirrors the on-state line and the gate junction of a TRIAC about zero.
constexpr double SignOf(const double value) {
    return value > 0.0 ? 1.0 : (value < 0.0 ? -1.0 : 0.0);
}

} // namespace

/**
 * @brief   Constructs a thyristor between its anode (MT2), cathode (MT1) and gate.
 * @param[in] anode      Anode (MT2) MNA node index.
 * @param[in] cathode    Cathode (MT1) MNA node index.
 * @param[in] gate       Gate MNA node index.
 * @param[in] parameters  On-state line, holding current, gate trigger and breakover parameters; `IsBidirectional`
 *                        makes it a TRIAC.
 */
ThyristorDevice::ThyristorDevice(const int anode,
                                 const int cathode,
                                 const int gate,
                                 const ThyristorParameters parameters)
    : m_Anode(anode), m_Cathode(cathode), m_Gate(gate), m_Parameters(parameters) {
}

/**
 * @brief   The piece of the on-state line a main-terminal voltage falls on.
 * @details An SCR follows the datasheet line \f$i = (v - V_0)/R_T\f$ at every voltage; a current it cannot carry
 *          releases it. A TRIAC mirrors that line about zero, and between the two thresholds, where a real device
 *          has not yet overcome its junction drop, it carries only the off-state current. The three pieces meet, so
 *          the line is continuous and rises with the voltage: one voltage for every current, whichever half the
 *          Newton iteration starts from. Mirroring by the sign of the voltage alone instead left the line, across
 *          zero, a second solution with a small voltage of one sign and a large current of the other.
 * @param[in] voltage  Main-terminal voltage, from the anode (MT2) to the cathode (MT1), in Volts.
 * @return  The conductance and the offset current of the piece.
 */
ThyristorDevice::BranchLine ThyristorDevice::OnStateLine(const double voltage) const {
    const double on_conductance = 1.0 / m_Parameters.OnResistance;
    const double off_conductance = 1.0 / m_Parameters.OffResistance;
    const double &threshold = m_Parameters.OnThresholdVoltage;
    if (!m_Parameters.IsBidirectional) {
        return {.Conductance = on_conductance, .Offset = -on_conductance * threshold};
    }
    if (std::fabs(voltage) <= threshold) {
        return {.Conductance = off_conductance, .Offset = 0.0};
    }
    // Past a threshold the line leaves the off-state current it reached there with the slope 1/RT
    const double sign = SignOf(voltage);
    return {.Conductance = on_conductance, .Offset = sign * threshold * (off_conductance - on_conductance)};
}

/**
 * @brief   Reports whether the gate junction conducts both ways.
 * @return  True for a TRIAC, whose gate fires it either way, and for a GTO, whose negative gate current turns it off.
 */
bool ThyristorDevice::IsGateMirrored() const {
    return m_Parameters.IsBidirectional || m_Parameters.GateTurnOffCurrent > 0.0;
}

/**
 * @brief   The current from the anode (MT2) to the cathode (MT1): the on-state line while latched, the blocking
 *          conductance otherwise.
 */
double ThyristorDevice::MainCurrent(const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Anode, m_Cathode);
    if (m_IsConducting) {
        const BranchLine line = OnStateLine(voltage);
        return line.Conductance * voltage + line.Offset;
    }
    return voltage / m_Parameters.OffResistance;
}

/**
 * @brief   The current into the gate from the cathode (MT1): the junction line above its threshold, zero below it.
 * @details A TRIAC or a GTO draws gate current of either polarity, so its junction mirrors about zero and the
 *          current keeps the sign of the gate voltage; a TRIAC fires on the magnitude in all four quadrants, a GTO
 *          fires on the positive current and releases on the negative one.
 */
double ThyristorDevice::GateCurrent(const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Gate, m_Cathode);
    const double &threshold = m_Parameters.GateTriggerVoltage;
    if (IsGateMirrored()) {
        return std::fabs(voltage) > threshold ? (voltage - SignOf(voltage) * threshold) / m_Parameters.GateResistance
                                              : 0.0;
    }
    return voltage > threshold ? (voltage - threshold) / m_Parameters.GateResistance : 0.0;
}

/**
 * @brief   Reads a figure the way the device does: on the magnitude for a TRIAC, on the signed value for an SCR.
 * @param[in] value  Voltage, current or slope to read.
 * @return  The absolute value for a TRIAC, the value itself for an SCR.
 */
double ThyristorDevice::Magnitude(const double value) const {
    return m_Parameters.IsBidirectional ? std::fabs(value) : value;
}

/**
 * @brief   The rate at which the main-terminal voltage rises from the last accepted point to the current iterate.
 * @param[in] voltage  Main-terminal voltage of the iterate, in Volts.
 * @return  The slope in Volts per second, or zero when no time has passed since the accepted point, as in a
 *          operating point or before the first step.
 */
double ThyristorDevice::VoltageSlope(const double voltage) const {
    const double elapsed = m_StampTime - m_Accepted.Time;
    return elapsed > 0.0 ? (voltage - m_Accepted.Voltage) / elapsed : 0.0;
}

/**
 * @brief   Reports whether the gate of the iterate sustains conduction.
 * @param[in] x  Current Newton iterate.
 * @return  True when the gate draws at least the trigger current, of either polarity for a TRIAC.
 */
bool ThyristorDevice::IsGateOn(const std::vector<double> &x) const {
    return Magnitude(GateCurrent(x)) >= m_Parameters.GateTriggerCurrent;
}

/**
 * @brief   Reports whether the main-terminal voltage of the iterate moves faster than the critical dv/dt.
 * @param[in] vak  Main-terminal voltage of the iterate, in Volts.
 * @return  True when the slope from the last accepted point reaches the critical rate, of either sign for a
 *          TRIAC; false when the figure is disabled.
 */
bool ThyristorDevice::IsPastCriticalDvDt(const double vak) const {
    return m_Parameters.CriticalDvDt > 0.0 && Magnitude(VoltageSlope(vak)) >= m_Parameters.CriticalDvDt;
}

/**
 * @brief   Decides whether a blocking device fires at the iterate.
 * @param[in] x              Current Newton iterate.
 * @param[in] is_recovering  Whether the turn-off recovery window is running.
 * @return  True on gate current, which an SCR only takes with a forward voltage, on breakover, on the dv/dt
 *          latch of the step, or on a voltage re-applied while recovering.
 */
bool ThyristorDevice::ShouldFire(const std::vector<double> &x, const bool is_recovering) const {
    const ThyristorParameters &p = m_Parameters;
    const double vak = BranchVoltage(x, m_Anode, m_Cathode);
    const double blocking = Magnitude(vak);
    const bool is_gate_fired = IsGateOn(x) && (p.IsBidirectional || vak > 0.0);
    const bool is_breakover = p.BreakoverVoltage > 0.0 && blocking >= p.BreakoverVoltage;
    const bool is_refired =
        is_recovering && blocking > p.OnThresholdVoltage && Magnitude(m_Accepted.Voltage) <= p.OnThresholdVoltage;
    return is_gate_fired || is_breakover || m_IsDvDtFired || is_refired;
}

/**
 * @brief   Reports whether the gate of a GTO pulls enough current out to turn it off.
 * @param[in] x  Current Newton iterate.
 * @return  True when the gate current is negative with a magnitude of at least the gate turn-off current; false for
 *          any device without one.
 */
bool ThyristorDevice::IsGateTurnOff(const std::vector<double> &x) const {
    return m_Parameters.GateTurnOffCurrent > 0.0 && -GateCurrent(x) >= m_Parameters.GateTurnOffCurrent;
}

/**
 * @brief   Decides whether a conducting device releases at the iterate.
 * @param[in] x              Current Newton iterate.
 * @param[in] is_recovering  Whether the turn-off recovery window is running, which suspends the release by the
 *                           holding current.
 * @return  True when a GTO gate pulls its turn-off current, whatever the main current, or when the gate is gone,
 *          the main current is below the holding current, the recovery window has closed and the dv/dt latch of the
 *          step is not sustaining conduction.
 */
bool ThyristorDevice::ShouldRelease(const std::vector<double> &x, const bool is_recovering) const {
    if (IsGateTurnOff(x)) {
        return true;
    }
    const bool is_held = Magnitude(MainCurrent(x)) >= m_Parameters.HoldingCurrent;
    return !IsGateOn(x) && !is_held && !is_recovering && !m_IsDvDtFired;
}

/**
 * @brief   Fires the latch on gate current, breakover, a fast voltage rise or a re-applied voltage during the
 *          recovery, and releases it below the holding current.
 * @details The latch itself is the hysteresis: once released, only a new gate pulse, breakover, a rise faster
 *          than the critical dv/dt or the recovery window fires again.
 *
 *          A rise faster than the critical dv/dt, read from the last accepted point to the iterate, latches for
 *          the rest of the step: it fires a blocking device and sustains conduction like a gate drive does, so
 *          the device cannot toggle within the step that carries the edge, and the Newton solve settles.
 *
 *          A release opens the recovery window for the turn-off time. While it runs, a blocking device re-fires
 *          when the main-terminal voltage rises back past the on-state threshold from at or below it at the last
 *          accepted point, which is the commutation failure of a forced-commutated circuit; a voltage already
 *          above the threshold when the device releases does not re-fire it, so the natural commutation of a
 *          rectifier still lands. While the window runs the device has not regained its blocking, so the
 *          holding-current release is suspended until it closes. A GTO turned off by its gate opens the same
 *          window, and stays off while the gate keeps pulling, since a negative gate never fires it.
 * @param[in] x  Current Newton iterate.
 */
void ThyristorDevice::UpdateState(const std::vector<double> &x) {
    const bool is_recovering = m_StampTime < m_RecoveryEnds;
    if (m_IsStep && IsPastCriticalDvDt(BranchVoltage(x, m_Anode, m_Cathode))) {
        m_IsDvDtFired = true;
    }
    if (m_IsConducting) {
        if (ShouldRelease(x, is_recovering)) {
            m_IsConducting = false;
            m_RecoveryEnds = m_StampTime + m_Parameters.TurnOffTime;
        }
        return;
    }
    if (ShouldFire(x, is_recovering)) {
        m_IsConducting = true;
    }
}

/**
 * @brief   Stamps the anode-cathode branch: the piece of the on-state line the iterate falls on, as a conductance
 *          with its offset current, or the blocking conductance.
 */
void ThyristorDevice::StampBranch(MnaSystem &sys, const std::vector<double> &x) const {
    if (m_IsConducting) {
        const BranchLine line = OnStateLine(BranchVoltage(x, m_Anode, m_Cathode));
        StampConductance(sys, m_Anode, m_Cathode, line.Conductance);
        // A source from cathode to anode takes its current away from the branch, so minus the offset adds it
        StampCurrentSource(sys, m_Cathode, m_Anode, -line.Offset);
    } else {
        StampConductance(sys, m_Anode, m_Cathode, 1.0 / m_Parameters.OffResistance);
    }
}

void ThyristorDevice::StampGate(MnaSystem &sys, const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Gate, m_Cathode);
    const double &threshold = m_Parameters.GateTriggerVoltage;
    const bool is_above = IsGateMirrored() ? std::fabs(voltage) > threshold : voltage > threshold;
    if (is_above) {
        const double conductance = 1.0 / m_Parameters.GateResistance + GATE_OFF_CONDUCTANCE;
        StampConductance(sys, m_Gate, m_Cathode, conductance);
        const double sign = IsGateMirrored() ? SignOf(voltage) : 1.0;
        StampCurrentSource(sys, m_Cathode, m_Gate, sign * threshold / m_Parameters.GateResistance);
    } else {
        StampConductance(sys, m_Gate, m_Cathode, GATE_OFF_CONDUCTANCE);
    }
}

/**
 * @brief   Decides the state at the iterate and stamps the branch and the gate.
 * @details The state is worked out here, from the solution the Newton iteration stamps at, so the converged one,
 *          which it stamps last, is the one AcceptStep() keeps. A new time point clears the dv/dt latch, which
 *          holds only for the step whose voltage edge fired it.
 * @param[in,out] sys   MNA system to stamp into.
 * @param[in]     x     Current Newton iterate.
 * @param[in]     time  Analysis time context.
 */
void ThyristorDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    if (time.Time != m_StampTime) {
        m_IsDvDtFired = false;
    }
    m_IsStep = time.IsTransient && !time.UseInitialConditions;
    m_StampTime = time.Time;
    UpdateState(x);
    StampBranch(sys, x);
    StampGate(sys, x);
}

double ThyristorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double gate_current = GateCurrent(x) + GATE_OFF_CONDUCTANCE * BranchVoltage(x, m_Gate, m_Cathode);
    switch (terminal) {
    case 0:
        return MainCurrent(x);
    case 1:
        // The cathode terminal carries the main branch and the gate branch, which both leave through it
        return -(MainCurrent(x) + gate_current);
    case 2:
        return gate_current;
    default:
        return 0.0;
    }
}

std::complex<double> ThyristorDevice::AcTerminalCurrent(const int terminal,
                                                        const std::vector<double> &x_dc,
                                                        const std::vector<std::complex<double>> &x_ac,
                                                        const double /*omega*/) const {
    const double branch_conductance =
        m_IsConducting ? 1.0 / m_Parameters.OnResistance : 1.0 / m_Parameters.OffResistance;
    const double gate_voltage = BranchVoltage(x_dc, m_Gate, m_Cathode);
    const bool gate_above = IsGateMirrored() ? std::fabs(gate_voltage) > m_Parameters.GateTriggerVoltage
                                             : gate_voltage > m_Parameters.GateTriggerVoltage;
    const double gate_conductance =
        gate_above ? 1.0 / m_Parameters.GateResistance + GATE_OFF_CONDUCTANCE : GATE_OFF_CONDUCTANCE;
    switch (terminal) {
    case 0:
        return branch_conductance * AcBranchVoltage(x_ac, m_Anode, m_Cathode);
    case 1:
        // The cathode terminal carries the main branch and the gate branch, which both leave through it
        return -(branch_conductance * AcBranchVoltage(x_ac, m_Anode, m_Cathode)
                 + gate_conductance * AcBranchVoltage(x_ac, m_Gate, m_Cathode));
    case 2:
        return gate_conductance * AcBranchVoltage(x_ac, m_Gate, m_Cathode);
    default:
        return {0.0, 0.0};
    }
}

/**
 * @brief   Keeps the state of the step that converged and moves the main-branch history on.
 * @details The first call after an operating point or an initial point has no step behind it: the history starts at
 *          the current and voltage of that point, so the commutation prediction waits for a second transient point.
 * @param[in] x  Converged solution vector of the accepted time point.
 */
void ThyristorDevice::AcceptStep(const std::vector<double> &x) {
    m_SavedIsConducting = m_IsConducting;
    m_SavedRecoveryEnds = m_RecoveryEnds;
    m_SavedIsDvDtFired = m_IsDvDtFired;
    const AcceptedPoint point{.Time = m_StampTime,
                              .Current = MainCurrent(x),
                              .Voltage = BranchVoltage(x, m_Anode, m_Cathode),
                              .GateCurrent = GateCurrent(x)};
    if (m_IsStep) {
        m_Previous = m_Accepted;
        m_Accepted = point;
        m_HasHistory = true;
    } else {
        m_Accepted = point;
        m_Previous = point;
        m_HasHistory = false;
    }
}

void ThyristorDevice::RejectStep() {
    m_IsConducting = m_SavedIsConducting;
    m_RecoveryEnds = m_SavedRecoveryEnds;
    m_IsDvDtFired = m_SavedIsDvDtFired;
}

bool ThyristorDevice::HasSwitched() const {
    return m_IsConducting != m_SavedIsConducting;
}

/**
 * @brief   Predicts the instant the main current falls to the holding current.
 * @details While conducting, the last two accepted points give the slope of the main current; extrapolated to the
 *          holding current, it predicts the commutation. When the current changes, the prediction is wrong and the
 *          transient asks again at the next point, so it closes in on the zero, as the fuse closes in on its blow.
 *          A TRIAC commutates on the magnitude, whichever way the current flows, so the prediction follows the
 *          magnitude toward the holding current.
 * @return  The predicted instant, or infinity when the current is not falling toward it.
 */
double ThyristorDevice::PredictHoldingCurrentCrossing() const {
    constexpr double NONE = std::numeric_limits<double>::infinity();
    const double elapsed = m_Accepted.Time - m_Previous.Time;
    if (elapsed <= 0.0) {
        return NONE;
    }
    const double slope = (m_Accepted.Current - m_Previous.Current) / elapsed;
    if (m_Parameters.IsBidirectional) {
        const double magnitude = std::fabs(m_Accepted.Current);
        const double magnitude_slope = m_Accepted.Current >= 0.0 ? slope : -slope;
        if (magnitude_slope >= 0.0 || magnitude <= m_Parameters.HoldingCurrent) {
            return NONE;
        }
        return m_Accepted.Time + (m_Parameters.HoldingCurrent - magnitude) / magnitude_slope;
    }
    if (slope >= 0.0 || m_Accepted.Current <= m_Parameters.HoldingCurrent) {
        return NONE;
    }
    return m_Accepted.Time + (m_Parameters.HoldingCurrent - m_Accepted.Current) / slope;
}

/**
 * @brief   Predicts the instant the gate of a GTO pulls its turn-off current.
 * @details The turn-off drive is the negative of the gate current; the last two accepted points give its slope, and
 *          extrapolating it to the gate turn-off current predicts the release, which the gate edge makes in a ramp.
 * @return  The predicted instant, or infinity for a device without a turn-off current or a drive that is not rising
 *          toward it.
 */
double ThyristorDevice::PredictGateTurnOffCrossing() const {
    constexpr double NONE = std::numeric_limits<double>::infinity();
    const double elapsed = m_Accepted.Time - m_Previous.Time;
    if (m_Parameters.GateTurnOffCurrent <= 0.0 || elapsed <= 0.0) {
        return NONE;
    }
    const double drive = -m_Accepted.GateCurrent;
    const double drive_slope = -(m_Accepted.GateCurrent - m_Previous.GateCurrent) / elapsed;
    if (drive_slope <= 0.0 || drive >= m_Parameters.GateTurnOffCurrent) {
        return NONE;
    }
    return m_Accepted.Time + (m_Parameters.GateTurnOffCurrent - drive) / drive_slope;
}

/**
 * @brief   Predicts the next instant the latch releases, so the transient lands on it.
 * @details The earlier of the holding-current crossing and, for a GTO, the gate turn-off crossing.
 * @param[in] time  Instant already reached in seconds.
 * @return  Next predicted release instant, or infinity when none is pending.
 */
double ThyristorDevice::NextBreakpoint(const double time) const {
    if (!m_IsConducting || !m_HasHistory) {
        return std::numeric_limits<double>::infinity();
    }
    const double crossing = std::min(PredictHoldingCurrentCrossing(), PredictGateTurnOffCrossing());
    return crossing - time >= MIN_BREAKPOINT_GAP ? crossing : std::numeric_limits<double>::infinity();
}

std::unique_ptr<Device> ThyristorDevice::Clone() const {
    return std::make_unique<ThyristorDevice>(*this);
}

} // namespace Nodalys