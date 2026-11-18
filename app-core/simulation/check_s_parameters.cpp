/**
 * @file    check_s_parameters.cpp
 * @brief   Checks the ports and the settings of an S-parameter analysis before it runs.
 */

#include "simulation/check_s_parameters.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_frequency_range.h"
#include <cstddef>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Core {

namespace {

/// Most points a linear S-parameter sweep may take.
constexpr int MAX_LINEAR_S_POINTS = 100000;

std::optional<std::string> CheckSParameterRange(const SParameterSettings &settings, const SParameterLimits &limits) {
    if (!settings.IsLogarithmic) {
        if (settings.StartFrequency <= 0.0) {
            return "The start frequency must be positive";
        }
        if (settings.StopFrequency <= settings.StartFrequency) {
            return "The stop frequency must be higher than the start frequency";
        }
        if (settings.LinearPoints < limits.MinLinearPoints || settings.LinearPoints > MAX_LINEAR_S_POINTS) {
            const std::string reason = limits.MinLinearPointsReason.empty() ? "" : "; " + limits.MinLinearPointsReason;
            return std::format("The number of points must be between {} and {}{}",
                               limits.MinLinearPoints,
                               MAX_LINEAR_S_POINTS,
                               reason);
        }
        return std::nullopt;
    }
    return CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade);
}

/**
 * @brief   Ports are numbered from 1 with no gap and no repeat, whatever the order they sit in on the schematic: the
 *          matrix and the Touchstone file index the ports by number, so a gap would leave a row with no port.
 * @note    The wording of this check and of the next one is the one the Nodalys engine uses for the same causes.
 */
std::optional<std::string> CheckSParameterPortNumbers(const std::vector<CircuitPort> &ports) {
    std::set<int> numbers;
    for (const CircuitPort &port : ports) {
        if (!numbers.insert(port.Number).second) {
            return std::format("Two S-parameter ports are numbered {}; give each port its own number", port.Number);
        }
    }
    for (int number = 1; number <= static_cast<int>(ports.size()); ++number) {
        if (!numbers.contains(number)) {
            return std::format("The S-parameter ports must be numbered 1 to {} with no gap; port {} is missing",
                               ports.size(),
                               number);
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckSParameterPortNodes(const std::vector<CircuitPort> &ports) {
    for (std::size_t first = 0; first < ports.size(); ++first) {
        const CircuitPort &port = ports[first];
        if (port.ReferenceOhms <= 0.0) {
            return std::format("The reference impedance of port {} must be positive", port.Number);
        }
        if (port.Node == 0) {
            return std::format("Port {} is on ground; an S-parameter port needs a node of its own", port.Number);
        }
        for (std::size_t second = first + 1; second < ports.size(); ++second) {
            if (ports[second].Node == port.Node) {
                return std::format("Ports {} and {} are on the same node; each port needs a node of its own",
                                   port.Number,
                                   ports[second].Number);
            }
        }
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks that a circuit has as many S-parameter ports as the backend asks for.
 * @param[in] port_count  Ports of the circuit.
 * @param[in] limits      What the backend asks, from BackendCapabilities::SParameters.
 * @return  The reason the ports are too few, with how to add one; no value when they are enough.
 */
std::optional<std::string> CheckSParameterPortCount(const std::size_t port_count, const SParameterLimits &limits) {
    if (port_count == 0) {
        return std::string("The circuit has no S-parameter port; turn on S-parameter port in the properties of a "
                           "Block pin marker");
    }
    if (port_count < limits.MinPorts) {
        return std::format("{}; turn on S-parameter port in the properties of another Block pin marker",
                           limits.MinPortsReason.empty()
                               ? std::format("The analysis needs at least {} S-parameter ports", limits.MinPorts)
                               : limits.MinPortsReason);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency range of an S-parameter analysis and its ports.
 * @param[in] circuit   Circuit to analyze; its ports are the pin markers set as ports.
 * @param[in] settings  Frequency range and points.
 * @param[in] limits    What the backend that will run it asks, from BackendCapabilities::SParameters.
 * @return  The reason the analysis cannot run, such as too few ports for the backend or ports numbered with a gap; no
 *          value when it can.
 * @note    ngspice stops for good when it finds fewer than two ports, so this must run before any backend.
 */
std::optional<std::string>
CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings, const SParameterLimits &limits) {
    if (std::optional<std::string> error = CheckSParameterRange(settings, limits)) {
        return error;
    }
    const std::vector<CircuitPort> &ports = circuit.GetPorts();
    if (std::optional<std::string> error = CheckSParameterPortCount(ports.size(), limits)) {
        return error;
    }
    if (std::optional<std::string> error = CheckSParameterPortNumbers(ports)) {
        return error;
    }
    return CheckSParameterPortNodes(ports);
}

} // namespace Core
