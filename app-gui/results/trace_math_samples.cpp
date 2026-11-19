/**
 * @file    trace_math_samples.cpp
 * @brief   Where the traces of a math channel come from: the samples of a transient or a DC sweep, or the phasors of an
 *          AC sweep.
 */

#include "results/trace_math_samples.h"

#include "results/node_index.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace GUI {

namespace {

/// Decibels of a ratio of amplitudes, such as voltages, per decade of the ratio.
constexpr double DECIBELS_PER_DECADE = 20.0;
constexpr double HALF_TURN_DEGREES = 180.0;

/**
 * @brief   Central differences inside, one-sided at the ends; repeated X values give a slope of zero.
 */
std::vector<double> Differentiate(const std::span<const double> xs, const std::vector<double> &values) {
    std::vector<double> slopes(values.size(), 0.0);
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t before = index == 0 ? 0 : index - 1;
        const std::size_t after = std::min(index + 1, values.size() - 1);
        const double width = xs[after] - xs[before];
        if (width != 0.0) {
            slopes[index] = (values[after] - values[before]) / width;
        }
    }
    return slopes;
}

/**
 * @brief   Running trapezoidal integral from the first sample, which starts at zero.
 */
std::vector<double> Integrate(const std::span<const double> xs, const std::vector<double> &values) {
    std::vector<double> integral(values.size(), 0.0);
    for (std::size_t index = 1; index < values.size(); ++index) {
        integral[index] = integral[index - 1] + (values[index - 1] + values[index]) / 2.0 * (xs[index] - xs[index - 1]);
    }
    return integral;
}

} // namespace

/**
 * @brief   Reads a transient or a DC sweep.
 * @param[in] xs             X value of each sample: times, or the values of a swept source.
 * @param[in] x_unit         Unit of the X values.
 * @param[in] node_voltages  Voltage of each node at each sample, indexed by node number.
 * @param[in] currents       Currents of the result, found by name.
 * @note    The result is referred to, not copied, so it has to outlive the samples.
 */
RealSamples::RealSamples(const std::span<const double> xs,
                         const Dimension x_unit,
                         const std::vector<std::vector<double>> &node_voltages,
                         const std::vector<Core::ComponentTrace> &currents)
    : m_Xs(xs), m_XUnit(x_unit), m_NodeVoltages(node_voltages), m_Currents(currents) {
}

/**
 * @brief   Counts the samples of every trace.
 * @return  The number of X values.
 */
std::size_t RealSamples::GetCount() const {
    return m_Xs.size();
}

/**
 * @brief   Gives the unit of the X values, which derivatives divide by and integrals multiply by.
 * @return  The unit.
 */
Dimension RealSamples::GetXUnit() const {
    return m_XUnit;
}

/**
 * @brief   Reads the voltage of a node.
 * @param[in] node  Node number, ground being 0.
 * @return  The voltage at every sample, or an error when the result has no such node.
 */
std::expected<std::vector<double>, std::string> RealSamples::GetVoltage(const int node) const {
    const std::optional<std::size_t> index = FindNodeIndex(node, m_NodeVoltages.size());
    if (!index) {
        return std::unexpected(std::format("The circuit has no node {}", node));
    }
    return m_NodeVoltages[*index];
}

/**
 * @brief   Reads the current of a part.
 * @param[in] name  Name of the current, such as "R1" or "Q1.C".
 * @return  The current at every sample, or an error when the result has none of that name.
 */
std::expected<std::vector<double>, std::string> RealSamples::GetCurrent(const std::string_view name) const {
    const auto current = std::ranges::find(m_Currents, name, &Core::ComponentTrace::Name);
    if (current == m_Currents.end()) {
        return std::unexpected(std::format("The circuit has no current I({})", name));
    }
    return current->Values;
}

/**
 * @brief   Differentiates a trace with respect to the X values.
 * @param[in] values  The trace, one value per sample.
 * @return  The slope at every sample.
 */
std::vector<double> RealSamples::GetDerivative(const std::vector<double> &values) const {
    return Differentiate(m_Xs, values);
}

/**
 * @brief   Integrates a trace with respect to the X values.
 * @param[in] values  The trace, one value per sample.
 * @return  The running integral at every sample, zero at the first.
 */
std::vector<double> RealSamples::GetIntegral(const std::vector<double> &values) const {
    return Integrate(m_Xs, values);
}

/**
 * @brief   Reads an AC sweep.
 * @param[in] sweep  The sweep, which has to outlive the samples.
 */
PhasorSamples::PhasorSamples(const Core::ACSweep &sweep) : m_Sweep(sweep) {
}

/**
 * @brief   Counts the frequencies of every trace.
 * @return  The number of frequencies.
 */
std::size_t PhasorSamples::GetCount() const {
    return m_Sweep.Frequencies.size();
}

/**
 * @brief   Gives the unit of the X values, which are frequencies, so a derivative divides by seconds.
 * @return  The unit.
 */
Dimension PhasorSamples::GetXUnit() {
    return {.Seconds = 1};
}

/**
 * @brief   Reads the voltage of a node.
 * @param[in] node  Node number, ground being 0.
 * @return  The phasor at every frequency, or an error when the sweep has no such node.
 */
std::expected<std::vector<PhasorSamples::Value>, std::string> PhasorSamples::GetVoltage(const int node) const {
    const std::optional<std::size_t> index = FindNodeIndex(node, m_Sweep.NodeMagnitudesDecibels.size());
    if (!index) {
        return std::unexpected(std::format("The circuit has no node {}", node));
    }
    // Ground keeps no samples in an AC sweep
    if (node == 0) {
        return std::vector<Value>(GetCount());
    }
    return ToPhasors(m_Sweep.NodeMagnitudesDecibels[*index], m_Sweep.NodePhasesDegrees[*index]);
}

/**
 * @brief   Reads the current of a part.
 * @param[in] name  Name of the current, such as "R1" or "Q1.C".
 * @return  The phasor at every frequency, or an error when the sweep has none of that name.
 */
std::expected<std::vector<PhasorSamples::Value>, std::string>
PhasorSamples::GetCurrent(const std::string_view name) const {
    const auto magnitude = std::ranges::find(m_Sweep.CurrentMagnitudesDecibels, name, &Core::ComponentTrace::Name);
    const auto phase = std::ranges::find(m_Sweep.CurrentPhasesDegrees, name, &Core::ComponentTrace::Name);
    if (magnitude == m_Sweep.CurrentMagnitudesDecibels.end() || phase == m_Sweep.CurrentPhasesDegrees.end()) {
        return std::unexpected(std::format("The circuit has no current I({})", name));
    }
    return ToPhasors(magnitude->Values, phase->Values);
}

/**
 * @brief   Differentiates a trace, which multiplies each phasor by j*omega.
 * @param[in] values  The trace, one phasor per frequency.
 * @return  The derivative at every frequency.
 */
std::vector<PhasorSamples::Value> PhasorSamples::GetDerivative(std::vector<Value> values) const {
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] *= GetJOmega(index);
    }
    return values;
}

/**
 * @brief   Integrates a trace, which divides each phasor by j*omega.
 * @param[in] values  The trace, one phasor per frequency.
 * @return  The integral at every frequency; not finite at 0 Hz.
 */
std::vector<PhasorSamples::Value> PhasorSamples::GetIntegral(std::vector<Value> values) const {
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] /= GetJOmega(index);
    }
    return values;
}

PhasorSamples::Value PhasorSamples::GetJOmega(const std::size_t index) const {
    return {0.0, 2.0 * std::numbers::pi * m_Sweep.Frequencies[index]};
}

std::vector<PhasorSamples::Value> PhasorSamples::ToPhasors(const std::vector<double> &magnitudes_decibels,
                                                           const std::vector<double> &phases_degrees) {
    std::vector<Value> phasors;
    phasors.reserve(magnitudes_decibels.size());
    for (std::size_t index = 0; index < magnitudes_decibels.size(); ++index) {
        phasors.push_back(std::polar(std::pow(10.0, magnitudes_decibels[index] / DECIBELS_PER_DECADE),
                                     phases_degrees[index] * std::numbers::pi / HALF_TURN_DEGREES));
    }
    return phasors;
}

/**
 * @brief   Reads a phasor the way an AC sweep stores it, the inverse of what PhasorSamples rebuilds.
 * @param[in] phasor  Complex value.
 * @return  Its magnitude in decibels, relative to 1 of its unit (20*log10), and its phase in degrees; NaN in both for a
 *          magnitude of zero or one that is not finite, which a plot skips.
 */
PhasorFigures ToDecibelsAndDegrees(const std::complex<double> phasor) {
    const double magnitude = std::abs(phasor);
    if (!std::isfinite(magnitude) || magnitude == 0.0) {
        return {.MagnitudeDecibels = std::numeric_limits<double>::quiet_NaN(),
                .PhaseDegrees = std::numeric_limits<double>::quiet_NaN()};
    }
    return {.MagnitudeDecibels = DECIBELS_PER_DECADE * std::log10(magnitude),
            .PhaseDegrees = std::arg(phasor) * HALF_TURN_DEGREES / std::numbers::pi};
}

} // namespace GUI
