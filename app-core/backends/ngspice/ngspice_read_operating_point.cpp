/**
 * @file    ngspice_read_operating_point.cpp
 * @brief   Reads the result of an operating point from the current plot of ngspice.
 */

#include "backends/ngspice/ngspice_read_operating_point.h"

#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/ratings.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <expected>
#include <format>
#include <string>

namespace Core {

/**
 * @brief   Reads the node voltages and component currents of an operating point.
 * @param[in] circuit  Circuit that was simulated, whose parts say which vectors to look for.
 * @return  The operating point, with rating warnings, or the error when a vector is missing.
 */
std::expected<OperatingPoint, std::string> ReadOperatingPoint(const Circuit &circuit) {
    OperatingPoint result;
    result.NodeVoltages.assign(static_cast<std::size_t>(circuit.GetNodeCount()), 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const auto voltage = ReadScalar(std::format("v({})", node), std::format("voltage for node {}", node));
        if (!voltage) {
            return std::unexpected(voltage.error());
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = *voltage;
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const auto value = ReadScalar(current.VectorName, std::format("current for {}", current.TraceName));
            if (!value) {
                return std::unexpected(value.error());
            }
            result.Currents.push_back({current.TraceName, *value});
        }
    }
    AddRatingWarnings(circuit, result);
    return result;
}

} // namespace Core
