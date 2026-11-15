/**
 * @file    ngspice_read_transient.cpp
 * @brief   Reads the result of a transient from the current plot of ngspice, whole or from a point on.
 */

#include "backends/ngspice/ngspice_read_transient.h"

#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/ratings.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Reads the node voltages and component currents of a transient over time.
 * @param[in] circuit  Circuit that was simulated.
 * @return  The transient, with rating warnings, or the error when a vector is missing.
 */
std::expected<Transient, std::string> ReadTransient(const Circuit &circuit) {
    std::expected<Transient, std::string> result = ReadTransientPoints(circuit, 0);
    if (result) {
        AddRatingWarnings(circuit, *result);
    }
    return result;
}

/**
 * @brief   Reads the points of a transient from one on, as they stand while ngspice has the run suspended.
 * @param[in] circuit      Circuit that is being simulated.
 * @param[in] first_point  Index of the first point to read; the ones before it were read already.
 * @return  The new points, none when there are no more, or the error when a vector is missing. It has no warnings,
 *          since the ratings are judged over the whole run.
 */
std::expected<Transient, std::string> ReadTransientPoints(const Circuit &circuit, const std::size_t first_point) {
    const pvector_info time = FindVector("time");
    if (time == nullptr) {
        return std::unexpected("ngspice returned no time points");
    }
    Transient result;
    result.Times = RealPart(*time, first_point);
    // time points to the record that the next lookup overwrites, so later vectors are checked against this length
    const std::size_t length = static_cast<std::size_t>(time->v_length);
    result.NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    result.NodeVoltages[0].assign(result.Times.size(), 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != length) {
            return std::unexpected(std::format("ngspice returned no voltages for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = RealPart(*vector, first_point);
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != length) {
                return std::unexpected(std::format("ngspice returned no currents for {}", current.TraceName));
            }
            result.Currents.push_back({current.TraceName, RealPart(*vector, first_point)});
        }
    }
    return result;
}

/**
 * @brief   Reads the time ngspice has reached in the transient it has in its current plot.
 * @return  The time of the last point, or no value when there are no points.
 */
std::optional<double> ReadTransientTime() {
    const pvector_info time = FindVector("time");
    if (time == nullptr) {
        return std::nullopt;
    }
    const std::size_t last = static_cast<std::size_t>(time->v_length) - 1;
    return time->v_realdata != nullptr ? time->v_realdata[last] : time->v_compdata[last].cx_real;
}

} // namespace Core
