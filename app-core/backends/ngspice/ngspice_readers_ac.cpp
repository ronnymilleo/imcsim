/**
 * @file    ngspice_readers_ac.cpp
 * @brief   Reads the results of the AC sweep from the current plot of ngspice.
 * @details Defines part of what ngspice_readers.h declares; the readers are split by kind of analysis over
 *          ngspice_vectors.h.
 */

#include "backends/ngspice/ngspice_readers.h"

#include "backends/ngspice/ngspice_library.h"
#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/components/source.h"
#include "simulation/circuit_checks.h"
#include "simulation/pole_zero.h"
#include "simulation/ratings.h"
#include "simulation/sensitivity.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <format>
#include <ngspice/sharedspice.h>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

namespace Core {

namespace {

/// 1e-15 V is -300 dB.
constexpr double MIN_MAGNITUDE = 1e-15;

/**
 * @brief   20 log10 of the magnitude, with a floor that keeps a value the AC sources do not reach finite for plotting.
 */
double ToDecibels(const std::complex<double> value) {
    return 20.0 * std::log10(std::max(std::abs(value), MIN_MAGNITUDE));
}

double ToDegrees(const std::complex<double> value) {
    return std::arg(value) * 180.0 / std::numbers::pi;
}

// ngspice computes no device currents in .ac, so resistors, capacitors and current sources are worked out from
// their node voltages and values; inductors and voltage sources have their branch current in the results. Probed
// parts are read from their probes instead
std::expected<std::vector<std::complex<double>>, std::string>
ReadACCurrent(const std::string &vector_name, const std::string &trace_name, const std::size_t point_count) {
    const pvector_info vector = FindVector(vector_name);
    if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
        return std::unexpected(std::format("ngspice returned no currents for {}", trace_name));
    }
    return ComplexValues(*vector);
}

void AddACCurrent(std::string name, const std::vector<std::complex<double>> &currents, ACSweep &result) {
    ComponentTrace magnitudes{name, {}};
    ComponentTrace phases{std::move(name), {}};
    std::ranges::transform(currents, std::back_inserter(magnitudes.Values), ToDecibels);
    std::ranges::transform(currents, std::back_inserter(phases.Values), ToDegrees);
    result.CurrentMagnitudesDecibels.push_back(std::move(magnitudes));
    result.CurrentPhasesDegrees.push_back(std::move(phases));
}

// Voltage across the part times its admittance: 1/R for a resistor, j*2*pi*f*C for a capacitor
std::vector<std::complex<double>>
PassiveACCurrent(const CircuitEntry &entry, const std::vector<double> &frequencies,
                 const std::vector<std::vector<std::complex<double>>> &node_voltages) {
    const Component &component = *entry.Part;
    std::vector<std::complex<double>> currents(frequencies.size());
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        const std::complex<double> voltage = node_voltages[static_cast<std::size_t>(entry.Nodes[0])][index] -
                                             node_voltages[static_cast<std::size_t>(entry.Nodes[1])][index];
        const std::complex<double> admittance =
            component.GetType() == ComponentType::RESISTOR
                ? std::complex<double>(1.0 / component.GetValue(), 0.0)
                : std::complex<double>(0.0, 2.0 * std::numbers::pi * frequencies[index] * component.GetValue());
        currents[index] = voltage * admittance;
    }
    return currents;
}

/**
 * @brief   The source drives its AC magnitude at zero phase, and nothing when it is not AC.
 */
std::vector<std::complex<double>> CurrentSourceACCurrent(const Component &component, const std::size_t point_count) {
    const auto &source = static_cast<const Source &>(component);
    const double magnitude = source.GetSourceType() == Source::SourceType::AC ? source.GetAC().Magnitude : 0.0;
    return std::vector<std::complex<double>>(point_count, std::complex<double>(magnitude, 0.0));
}

bool HasACBranchCurrent(const ComponentType type) {
    static constexpr std::array BRANCH_CURRENT_TYPES = {ComponentType::INDUCTOR, ComponentType::VCC,
                                                        ComponentType::VOLTAGE_SOURCE, ComponentType::CLOCK,
                                                        ComponentType::LOGIC_INPUT};
    return std::ranges::contains(BRANCH_CURRENT_TYPES, type);
}

std::expected<std::vector<std::complex<double>>, std::string>
ACCurrent(const CircuitEntry &entry, const std::vector<double> &frequencies,
          const std::vector<std::vector<std::complex<double>>> &node_voltages) {
    const Component &component = *entry.Part;
    const ComponentType type = component.GetType();
    if (type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR) {
        return PassiveACCurrent(entry, frequencies, node_voltages);
    }
    if (type == ComponentType::CURRENT_SOURCE) {
        return CurrentSourceACCurrent(component, frequencies.size());
    }
    if (HasACBranchCurrent(type)) {
        return ReadACCurrent(BranchVectorName(component.GetName()), component.GetName(), frequencies.size());
    }
    return std::unexpected(std::format("{} carries no current", component.GetName()));
}

} // namespace

/**
 * @brief   Reads the magnitude and phase of every node and current of an AC sweep.
 * @param[in] circuit  Circuit that was simulated.
 * @return  The sweep or the error when a vector is missing.
 */
std::expected<ACSweep, std::string> ReadACSweep(const Circuit &circuit) {
    const pvector_info frequency = FindVector("frequency");
    if (frequency == nullptr) {
        return std::unexpected("ngspice returned no frequency points");
    }
    ACSweep result;
    result.Frequencies = RealPart(*frequency);
    const std::size_t point_count = result.Frequencies.size();

    // Ground stays at zero, which the current of a component connected to it needs
    std::vector<std::vector<std::complex<double>>> node_voltages(static_cast<std::size_t>(circuit.GetNodeCount()));
    node_voltages[0].assign(point_count, 0.0);
    result.NodeMagnitudesDecibels.resize(node_voltages.size());
    result.NodePhasesDegrees.resize(node_voltages.size());
    for (std::size_t node = 1; node < node_voltages.size(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
            return std::unexpected(std::format("ngspice returned no response for node {}", node));
        }
        node_voltages[node] = ComplexValues(*vector);
        std::ranges::transform(node_voltages[node], std::back_inserter(result.NodeMagnitudesDecibels[node]),
                               ToDecibels);
        std::ranges::transform(node_voltages[node], std::back_inserter(result.NodePhasesDegrees[node]), ToDegrees);
    }

    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const std::vector<CurrentVector> vectors = CurrentVectors(*entry.Part);
        if (!GetCurrentProbes(*entry.Part).empty()) {
            for (const CurrentVector &current : vectors) {
                const auto currents = ReadACCurrent(current.VectorName, current.TraceName, point_count);
                if (!currents) {
                    return std::unexpected(currents.error());
                }
                AddACCurrent(current.TraceName, *currents, result);
            }
            continue;
        }
        if (vectors.empty()) {
            continue;
        }
        // A supply rail has one terminal; its other side is ground
        CircuitEntry two_terminal = entry;
        two_terminal.Nodes.resize(2, 0);
        const auto currents = ACCurrent(two_terminal, result.Frequencies, node_voltages);
        if (!currents) {
            return std::unexpected(currents.error());
        }
        AddACCurrent(entry.Part->GetName(), *currents, result);
    }
    return result;
}

} // namespace Core
