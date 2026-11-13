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
#include <ngspice/sharedspice.h>
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
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || vector->v_realdata == nullptr) {
            return std::unexpected(std::format("ngspice returned no voltage for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = vector->v_realdata[0];
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || vector->v_realdata == nullptr) {
                return std::unexpected(std::format("ngspice returned no current for {}", current.TraceName));
            }
            result.Currents.push_back({current.TraceName, vector->v_realdata[0]});
        }
    }
    AddRatingWarnings(circuit, result);
    return result;
}

} // namespace Core
