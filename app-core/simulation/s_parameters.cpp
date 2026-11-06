/**
 * @file    s_parameters.cpp
 * @brief   The netlist lines of an S-parameter analysis, the quantities read from a reflection, and what went wrong
 *          when the solver gave no matrix.
 */

#include "simulation/s_parameters.h"

#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>

namespace Core {

namespace {

/// Below this distance from 1 a reflection is an open circuit as far as the impedance goes.
constexpr double OPEN_CIRCUIT_TOLERANCE = 1e-12;

constexpr std::string_view FATAL_PREFIX = "Fatal error:";
constexpr std::string_view ERROR_PREFIX = "ERROR:";

} // namespace

/**
 * @brief   Writes the source that drives and terminates each port.
 * @param[in] ports  Ports of the circuit, whose numbers are 1 to N without repeats.
 * @return  One line per port: a 0 V source from the node to ground, with the port number and reference impedance that
 *          ngspice's S-parameter analysis reads. The source carries the reference impedance in series, so it loads the
 *          circuit; that is why ports exist only in this analysis.
 */
std::string FormatPortSources(const std::vector<CircuitPort> &ports) {
    std::string lines;
    for (const CircuitPort &port : ports) {
        lines += std::format("Vsport{0} {1} 0 dc 0 portnum {0} z0 {2}\n", port.Number, port.Node,
                             FormatSpiceValue(port.ReferenceOhms));
    }
    return lines;
}

/**
 * @brief   Writes the .sp card of an S-parameter analysis.
 * @param[in] settings  Frequency range and points, already checked.
 * @return  The card, such as ".sp dec 20 1meg 1g" or ".sp lin 101 1meg 1g".
 */
std::string FormatSParameterAnalysis(const SParameterSettings &settings) {
    return std::format(".sp {} {} {} {}", settings.IsLogarithmic ? "dec" : "lin",
                       settings.IsLogarithmic ? settings.PointsPerDecade : settings.LinearPoints,
                       FormatSpiceValue(settings.StartFrequency), FormatSpiceValue(settings.StopFrequency));
}

/**
 * @brief   Converts a complex value to decibels of its magnitude.
 * @param[in] value  Complex value, such as an S parameter.
 * @return  20 log10 of the magnitude, floored at -300 dB.
 */
double ToDecibels(const std::complex<double> value) {
    return 20.0 * std::log10(std::max(std::abs(value), MIN_S_MAGNITUDE));
}

/**
 * @brief   Converts the argument of a complex value to degrees.
 * @param[in] value  Complex value.
 * @return  The angle in degrees, from -180 to 180.
 */
double ToDegrees(const std::complex<double> value) {
    return std::arg(value) * 180.0 / std::numbers::pi;
}

/**
 * @brief   Finds the impedance a port sees from its reflection coefficient.
 * @param[in] reflection      S(i, i) of the port, with the other ports terminated in their references.
 * @param[in] reference_ohms  Reference impedance of the port.
 * @return  Z0 (1 + S) / (1 - S) in ohms; no value for an open circuit (S = 1), whose impedance is infinite.
 */
std::optional<std::complex<double>> ToImpedance(const std::complex<double> reflection, const double reference_ohms) {
    const std::complex<double> denominator = 1.0 - reflection;
    if (std::abs(denominator) < OPEN_CIRCUIT_TOLERANCE) {
        return std::nullopt;
    }
    return reference_ohms * (1.0 + reflection) / denominator;
}

/**
 * @brief   Computes the voltage standing wave ratio of a reflection.
 * @param[in] reflection  S(i, i) of the port.
 * @return  (1 + |S|) / (1 - |S|); no value when |S| reaches 1, where the ratio is infinite.
 */
std::optional<double> ComputeVswr(const std::complex<double> reflection) {
    const double magnitude = std::abs(reflection);
    if (magnitude >= 1.0 - OPEN_CIRCUIT_TOLERANCE) {
        return std::nullopt;
    }
    return (1.0 + magnitude) / (1.0 - magnitude);
}

/**
 * @brief   Computes the return loss of a reflection.
 * @param[in] reflection  S(i, i) of the port.
 * @return  -20 log10 |S| in decibels, positive for a port that absorbs power; no value for a perfect match (S = 0),
 *          whose return loss is infinite.
 */
std::optional<double> ComputeReturnLossDecibels(const std::complex<double> reflection) {
    if (std::abs(reflection) < MIN_S_MAGNITUDE) {
        return std::nullopt;
    }
    return -ToDecibels(reflection);
}

/**
 * @brief   Finds why ngspice gave no S matrix.
 * @param[in] messages  Lines the simulator printed.
 * @return  The first fatal error or error line in words for the user; no value when ngspice said none.
 */
std::optional<std::string> DescribeSParameterFailure(const std::vector<SimulatorMessage> &messages) {
    for (const SimulatorMessage &message : messages) {
        if (message.Text.starts_with(FATAL_PREFIX) || message.Text.starts_with(ERROR_PREFIX)) {
            return std::format("ngspice refused the S-parameter analysis: {}", message.Text);
        }
    }
    return std::nullopt;
}

} // namespace Core
