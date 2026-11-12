/**
 * @file    ngspice_readers.cpp
 * @brief   Reads the results of the operating point, the transient and the DC sweep from the current plot of ngspice.
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

/**
 * @brief   Reads the curves of a DC sweep, one per value of the stepped source.
 * @details The swept values repeat once per value of the stepped source, so the curves are the stretches between
 *          repeats.
 * @param[in] circuit    Circuit that was simulated.
 * @param[in] settings   Swept and stepped sources the run used.
 * @return  The sweep or the error when a vector is missing.
 */
std::expected<DCSweep, std::string> ReadDCSweep(const Circuit &circuit, const DCSweepSettings &settings) {
    const Component &swept = *FindSweepableSource(circuit, settings.Swept.Source);
    const pvector_info scale = FindVector(swept.GetType() == ComponentType::CURRENT_SOURCE ? "i-sweep" : "v-sweep");
    if (scale == nullptr) {
        return std::unexpected("ngspice returned no swept values");
    }
    const std::vector<double> all_values = RealPart(*scale);
    const auto repeat = std::ranges::find(all_values.begin() + 1, all_values.end(), all_values.front());
    const auto curve_length = static_cast<std::size_t>(repeat - all_values.begin());
    if (all_values.size() % curve_length != 0) {
        return std::unexpected("ngspice returned curves of different lengths");
    }

    DCSweep result;
    result.SweptSource = swept.GetName();
    result.SweptUnit = SourceUnit(swept);
    result.SweptValues.assign(all_values.begin(), repeat);
    if (settings.Stepped) {
        result.SteppedSource = settings.Stepped->Source;
        result.SteppedUnit = SourceUnit(*FindSweepableSource(circuit, settings.Stepped->Source));
    }
    const std::size_t curve_count = all_values.size() / curve_length;
    result.Curves.resize(curve_count);
    for (std::size_t curve = 0; curve < curve_count; ++curve) {
        if (settings.Stepped) {
            result.Curves[curve].StepValue =
                settings.Stepped->Start + static_cast<double>(curve) * settings.Stepped->Step;
        }
        result.Curves[curve].NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    }

    // Splits a vector that holds every curve back to back
    const auto split = [&](const std::vector<double> &values, const auto &store) {
        for (std::size_t curve = 0; curve < curve_count; ++curve) {
            const auto begin = values.begin() + static_cast<std::ptrdiff_t>(curve * curve_length);
            store(result.Curves[curve], std::vector<double>(begin, begin + static_cast<std::ptrdiff_t>(curve_length)));
        }
    };
    for (int node = 0; node < circuit.GetNodeCount(); ++node) {
        std::vector<double> voltages(all_values.size(), 0.0);
        if (node > 0) {
            const pvector_info vector = FindVector(std::format("v({})", node));
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != all_values.size()) {
                return std::unexpected(std::format("ngspice returned no voltages for node {}", node));
            }
            voltages = RealPart(*vector);
        }
        split(voltages, [node](DCSweepCurve &curve, std::vector<double> values) {
            curve.NodeVoltages[static_cast<std::size_t>(node)] = std::move(values);
        });
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != all_values.size()) {
                return std::unexpected(std::format("ngspice returned no currents for {}", current.TraceName));
            }
            split(RealPart(*vector), [&current](DCSweepCurve &curve, std::vector<double> values) {
                curve.Currents.push_back({current.TraceName, std::move(values)});
            });
        }
    }
    return result;
}

} // namespace Core
