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

/// Fewest points a linear S-parameter sweep may take: ngspice gives a single point, at the start, for two.
constexpr int MIN_LINEAR_S_POINTS = 3;

/// Most points a linear S-parameter sweep may take.
constexpr int MAX_LINEAR_S_POINTS = 100000;

std::optional<std::string> CheckSParameterRange(const SParameterSettings &settings) {
    if (!settings.IsLogarithmic) {
        if (settings.StartFrequency <= 0.0) {
            return "The start frequency must be positive";
        }
        if (settings.StopFrequency <= settings.StartFrequency) {
            return "The stop frequency must be higher than the start frequency";
        }
        if (settings.LinearPoints < MIN_LINEAR_S_POINTS || settings.LinearPoints > MAX_LINEAR_S_POINTS) {
            return std::format("The number of points must be between {} and {}; ngspice gives only the first point "
                               "for two",
                               MIN_LINEAR_S_POINTS, MAX_LINEAR_S_POINTS);
        }
        return std::nullopt;
    }
    return CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade);
}

/**
 * @brief   Ports are numbered from 1 with no gap and no repeat, which ngspice requires and reports as a fatal error,
 *          while ports on one node it takes without a word.
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
                               ports.size(), number);
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
                                   port.Number, ports[second].Number);
            }
        }
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks the frequency range of an S-parameter analysis and its ports.
 * @param[in] circuit   Circuit to analyze; its ports are the pin markers set as ports.
 * @param[in] settings  Frequency range and points.
 * @return  The reason the analysis cannot run, such as fewer than two ports or ports numbered with a gap; no value
 *          when it can.
 * @note    ngspice stops for good when it finds fewer than two ports, so this must run before it.
 */
std::optional<std::string> CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings) {
    if (std::optional<std::string> error = CheckSParameterRange(settings)) {
        return error;
    }
    const std::vector<CircuitPort> &ports = circuit.GetPorts();
    if (ports.size() < MIN_S_PARAMETER_PORTS) {
        return "ngspice needs at least two S-parameter ports; turn on S-parameter port in the properties of two or "
               "more "
               "Block pin markers";
    }
    if (std::optional<std::string> error = CheckSParameterPortNumbers(ports)) {
        return error;
    }
    return CheckSParameterPortNodes(ports);
}

} // namespace Core
