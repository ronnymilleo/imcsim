/**
 * @file    thyristor_device.cpp
 * @brief   Thyristor (SCR) of Nodalys: a latching switch fired by gate current and held on by its anode current.
 */

#include "devices/semiconductors/thyristor_device.h"

#include "devices/stamp_helpers.h"
#include <limits>

namespace Nodalys {

namespace {

/// Gate-cathode leakage that keeps the gate node tied to the cathode when the junction is below its threshold, so a
/// gate left undriven never floats the matrix.
constexpr double GATE_OFF_CONDUCTANCE = 1e-9;
/// Closest a predicted commutation may lie ahead of the time for the transient to land on it; nearer, the step passes
/// it and the error is that small a share of a period.
constexpr double MIN_BREAKPOINT_GAP = 1e-12;

} // namespace

/**
 * @brief   Constructs a thyristor between its anode, cathode and gate.
 * @param[in] anode      Anode MNA node index.
 * @param[in] cathode    Cathode MNA node index.
 * @param[in] gate       Gate MNA node index.
 * @param[in] parameters  On-state line, holding current, gate trigger and breakover parameters.
 */
ThyristorDevice::ThyristorDevice(const int anode,
                                 const int cathode,
                                 const int gate,
                                 const ThyristorParameters parameters)
    : m_Anode(anode), m_Cathode(cathode), m_Gate(gate), m_Parameters(parameters) {
}

double ThyristorDevice::AnodeCurrent(const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Anode, m_Cathode);
    if (m_IsConducting) {
        return (voltage - m_Parameters.OnThresholdVoltage) / m_Parameters.OnResistance;
    }
    return voltage / m_Parameters.OffResistance;
}

double ThyristorDevice::GateCurrent(const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Gate, m_Cathode);
    return voltage > m_Parameters.GateTriggerVoltage
             ? (voltage - m_Parameters.GateTriggerVoltage) / m_Parameters.GateResistance
             : 0.0;
}

/**
 * @brief   Fires the latch on gate current or forward breakover, and releases it below the holding current.
 * @details While the gate keeps drawing at least the trigger current it sustains conduction whatever the anode
 *          current; with the gate removed the latch holds only while the anode current stays above the holding
 *          current. The latch itself is the hysteresis: once released, only a new gate pulse or breakover fires again.
 */
void ThyristorDevice::UpdateState(const std::vector<double> &x) {
    const ThyristorParameters &p = m_Parameters;
    const double vak = BranchVoltage(x, m_Anode, m_Cathode);
    const double gate = GateCurrent(x);
    if (!m_IsConducting) {
        const bool is_gate_fired = vak > 0.0 && gate >= p.GateTriggerCurrent;
        const bool is_breakover = p.BreakoverVoltage > 0.0 && vak >= p.BreakoverVoltage;
        if (is_gate_fired || is_breakover) {
            m_IsConducting = true;
        }
    } else if (gate < p.GateTriggerCurrent && AnodeCurrent(x) < p.HoldingCurrent) {
        m_IsConducting = false;
    }
}

/**
 * @brief   Stamps the anode-cathode branch: the on-state line as a conductance with its threshold source, or the
 *          blocking conductance.
 */
void ThyristorDevice::StampBranch(MnaSystem &sys) const {
    if (m_IsConducting) {
        const double conductance = 1.0 / m_Parameters.OnResistance;
        StampConductance(sys, m_Anode, m_Cathode, conductance);
        // The on-state line I = (V - V0) / RT is the conductance 1/RT less a constant V0/RT, which a source from
        // cathode to anode supplies
        StampCurrentSource(sys, m_Cathode, m_Anode, conductance * m_Parameters.OnThresholdVoltage);
    } else {
        StampConductance(sys, m_Anode, m_Cathode, 1.0 / m_Parameters.OffResistance);
    }
}

void ThyristorDevice::StampGate(MnaSystem &sys, const std::vector<double> &x) const {
    const double voltage = BranchVoltage(x, m_Gate, m_Cathode);
    if (voltage > m_Parameters.GateTriggerVoltage) {
        const double conductance = 1.0 / m_Parameters.GateResistance + GATE_OFF_CONDUCTANCE;
        StampConductance(sys, m_Gate, m_Cathode, conductance);
        StampCurrentSource(sys, m_Cathode, m_Gate, m_Parameters.GateTriggerVoltage / m_Parameters.GateResistance);
    } else {
        StampConductance(sys, m_Gate, m_Cathode, GATE_OFF_CONDUCTANCE);
    }
}

/**
 * @brief   Decides the state at the iterate and stamps the branch and the gate.
 * @details The state is worked out here, from the solution the Newton iteration stamps at, so the converged one,
 *          which it stamps last, is the one AcceptStep() keeps.
 * @param[in,out] sys   MNA system to stamp into.
 * @param[in]     x     Current Newton iterate.
 * @param[in]     time  Analysis time context.
 */
void ThyristorDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    m_IsStep = time.IsTransient && !time.UseInitialConditions;
    m_StampTime = time.Time;
    UpdateState(x);
    StampBranch(sys);
    StampGate(sys, x);
}

double ThyristorDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    switch (terminal) {
    case 0:
        return AnodeCurrent(x);
    case 1:
        return -AnodeCurrent(x);
    case 2:
        return GateCurrent(x) + GATE_OFF_CONDUCTANCE * BranchVoltage(x, m_Gate, m_Cathode);
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
    switch (terminal) {
    case 0:
        return branch_conductance * AcBranchVoltage(x_ac, m_Anode, m_Cathode);
    case 1:
        return -branch_conductance * AcBranchVoltage(x_ac, m_Anode, m_Cathode);
    case 2: {
        const double gate_conductance = BranchVoltage(x_dc, m_Gate, m_Cathode) > m_Parameters.GateTriggerVoltage
                                          ? 1.0 / m_Parameters.GateResistance + GATE_OFF_CONDUCTANCE
                                          : GATE_OFF_CONDUCTANCE;
        return gate_conductance * AcBranchVoltage(x_ac, m_Gate, m_Cathode);
    }
    default:
        return {0.0, 0.0};
    }
}

/**
 * @brief   Keeps the state of the step that converged and moves the anode-current history on.
 * @details The first call after an operating point or an initial point has no step behind it: the history starts at
 *          the current of that point, so the commutation prediction waits for a second transient point.
 * @param[in] x  Converged solution vector of the accepted time point.
 */
void ThyristorDevice::AcceptStep(const std::vector<double> &x) {
    m_SavedIsConducting = m_IsConducting;
    const AcceptedPoint point{.Time = m_StampTime, .Current = AnodeCurrent(x)};
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
}

bool ThyristorDevice::HasSwitched() const {
    return m_IsConducting != m_SavedIsConducting;
}

/**
 * @brief   Predicts the instant the anode current falls to the holding current, so the transient lands on it.
 * @details While conducting, the last two accepted points give the slope of the anode current; extrapolated to the
 *          holding current, it predicts the commutation. When the current changes, the prediction is wrong and the
 *          transient asks again at the next point, so it closes in on the zero, as the fuse closes in on its blow.
 * @param[in] time  Instant already reached in seconds.
 * @return  Next predicted commutation instant, or infinity when none is pending.
 */
double ThyristorDevice::NextBreakpoint(const double time) const {
    if (!m_IsConducting || !m_HasHistory) {
        return std::numeric_limits<double>::infinity();
    }
    const double elapsed = m_Accepted.Time - m_Previous.Time;
    if (elapsed <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const double slope = (m_Accepted.Current - m_Previous.Current) / elapsed;
    if (slope >= 0.0 || m_Accepted.Current <= m_Parameters.HoldingCurrent) {
        return std::numeric_limits<double>::infinity();
    }
    const double crossing = m_Accepted.Time + (m_Parameters.HoldingCurrent - m_Accepted.Current) / slope;
    return crossing - time >= MIN_BREAKPOINT_GAP ? crossing : std::numeric_limits<double>::infinity();
}

std::unique_ptr<Device> ThyristorDevice::Clone() const {
    return std::make_unique<ThyristorDevice>(*this);
}

} // namespace Nodalys
