/**
 * @file    fuse_device.cpp
 * @brief   Fuse of Nodalys: a resistance that heats with the current squared, opens for good and stays open.
 */

#include "devices/fuse_device.h"

#include "devices/semiconductor_math.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Nodalys {

namespace {

/// Heat, in units of what the rated current settles at, where the fuse opens. A margin above 1 keeps a fuse at its
/// rating from ever blowing, as the latch of the ngspice model does.
constexpr double BLOW_HEAT = 1.015;
/// Relative distance below BLOW_HEAT that still counts as reached, so a step that lands on the predicted instant does
/// not miss it by rounding.
constexpr double BLOW_HEAT_TOLERANCE = 1e-9;
/// How long the resistance takes to go from cold to blown, as a share of the cooling time constant. The ngspice model
/// charges its latch in about 5e-5 of it, and its resistance crosses the geometric mean halfway.
constexpr double OPENING_TIME_SHARE = 1e-4;
/// Closest a corner may lie ahead of the time for the transient to land on it; nearer, the step passes it and the
/// error is that small a share of the time constant.
constexpr double MIN_BREAKPOINT_SHARE = 1e-6;

/// Smooth step from 0 to 1, with a zero slope at both ends.
double SmoothStep(const double fraction) {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

} // namespace

FuseDevice::FuseDevice(const int node_a, const int node_b, const FuseParameters parameters)
    : m_NodeA(node_a), m_NodeB(node_b), m_Parameters(parameters),
      m_CoolingTimeConstant(parameters.MeltingI2t / (parameters.RatedCurrent * parameters.RatedCurrent)),
      m_Resistance(parameters.ColdResistance) {
}

/**
 * @brief   Gives the resistance of the element at an instant, from the state the step started in.
 * @details Cold until the fuse blows, then the geometric interpolation from the cold to the blown value along a
 *          smooth step, which keeps the resistance changing by decades over the opening time and no faster.
 */
double FuseDevice::ResistanceAt(const State &state, const double time) const {
    if (!state.IsBlown) {
        return m_Parameters.ColdResistance;
    }
    const double opening_time = OPENING_TIME_SHARE * m_CoolingTimeConstant;
    const double progress = SmoothStep((time - state.BlowTime) / opening_time);
    return m_Parameters.ColdResistance * std::pow(m_Parameters.BlownResistance / m_Parameters.ColdResistance, progress);
}

double FuseDevice::PowerOf(const std::vector<double> &x, const double resistance) const {
    const double current = (NodeVoltage(x, m_NodeA) - NodeVoltage(x, m_NodeB)) / resistance;
    return current * current / (m_Parameters.RatedCurrent * m_Parameters.RatedCurrent);
}

/**
 * @brief   Moves the state to the end of a step.
 * @details With the power taken as the mean of its values at the two ends, the heat equation is linear and has the
 *          exact solution h(t) = P + (h0 - P) exp(-(t - t0) / tau), which holds however long the step is. The fuse
 *          blows in the step where the heat reaches BLOW_HEAT, at the instant that solution crosses it.
 * @param[in] state      State at the start of the step.
 * @param[in] time       Instant the step ends at.
 * @param[in] end_power  Power at that instant.
 * @return  The state at the end of the step.
 */
FuseDevice::State FuseDevice::Advance(const State &state, const double time, const double end_power) const {
    const double elapsed = time - state.Time;
    if (elapsed <= 0.0) {
        return state;
    }
    const double mean_power = 0.5 * (state.Power + end_power);
    State next = state;
    next.Time = time;
    next.Power = end_power;
    next.Heat = mean_power + (state.Heat - mean_power) * std::exp(-elapsed / m_CoolingTimeConstant);
    const bool has_reached_blow_heat = next.Heat >= BLOW_HEAT * (1.0 - BLOW_HEAT_TOLERANCE) && mean_power > BLOW_HEAT;
    if (!state.IsBlown && has_reached_blow_heat) {
        next.IsBlown = true;
        const double crossing = -m_CoolingTimeConstant * std::log((mean_power - BLOW_HEAT) / (mean_power - state.Heat));
        next.BlowTime = std::clamp(state.Time + crossing, state.Time, time);
    }
    return next;
}

/**
 * @brief   Stamps the resistance of the instant and works out the state the step ends in.
 * @details Outside a transient step, and at its initial point, the fuse is a cold resistance: an operating point, a
 *          DC sweep and the small-signal analyses always see a whole fuse. The pending state is computed here, from the
 *          solution the Newton iteration stamps at, so that the converged one, which it stamps last, is the one that
 *          AcceptStep() keeps.
 */
void FuseDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    m_IsStep = time.IsTransient && !time.UseInitialConditions;
    m_Resistance = m_IsStep ? ResistanceAt(m_Accepted, time.Time) : m_Parameters.ColdResistance;
    StampConductance(sys, m_NodeA, m_NodeB, 1.0 / m_Resistance);
    m_Pending = m_IsStep ? Advance(m_Accepted, time.Time, PowerOf(x, m_Resistance)) : m_Accepted;
}

double FuseDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = (NodeVoltage(x, m_NodeA) - NodeVoltage(x, m_NodeB)) / m_Resistance;
    return terminal == 0 ? current : -current;
}

std::complex<double> FuseDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &x_ac,
                                                   const double /*omega*/) const {
    const std::complex<double> voltage = AcNodeVoltage(x_ac, m_NodeA) - AcNodeVoltage(x_ac, m_NodeB);
    const std::complex<double> current = voltage / m_Parameters.ColdResistance;
    return terminal == 0 ? current : -current;
}

/**
 * @brief   Keeps the state of the step that converged, or starts the run from a whole, cold fuse.
 * @details The first call after an operating point or an initial point has no step to advance: the heat starts at zero,
 *          as the gate of the ngspice model holds it until the transient starts.
 */
void FuseDevice::AcceptStep(const std::vector<double> &x) {
    if (m_IsStep) {
        m_Accepted = m_Pending;
        return;
    }
    m_Accepted = State{.Power = PowerOf(x, m_Parameters.ColdResistance)};
    m_Pending = m_Accepted;
}

void FuseDevice::RejectStep() {
    m_Pending = m_Accepted;
}

bool FuseDevice::HasSwitched() const {
    return m_Pending.IsBlown != m_Accepted.IsBlown;
}

/**
 * @brief   Predicts the next instant the fuse changes, so the transient lands a time point on it.
 * @details Before the blow, that is the instant the heat reaches BLOW_HEAT if the current stays what it is at the last
 *          point: the closed form of the heat equation at a constant power. When the current changes, the prediction
 *          is wrong and the transient asks again at the next point, so it closes in on the blow. After it, the
 *          instant the resistance has reached its blown value.
 */
double FuseDevice::NextBreakpoint(const double time) const {
    const double min_gap = MIN_BREAKPOINT_SHARE * m_CoolingTimeConstant;
    double next = std::numeric_limits<double>::infinity();
    if (m_Accepted.IsBlown) {
        next = m_Accepted.BlowTime + OPENING_TIME_SHARE * m_CoolingTimeConstant;
    } else if (m_Accepted.Power > BLOW_HEAT && m_Accepted.Heat < BLOW_HEAT) {
        next = m_Accepted.Time -
               m_CoolingTimeConstant * std::log((m_Accepted.Power - BLOW_HEAT) / (m_Accepted.Power - m_Accepted.Heat));
    }
    return next - time >= min_gap ? next : std::numeric_limits<double>::infinity();
}

} // namespace Nodalys
