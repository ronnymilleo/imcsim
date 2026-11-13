/**
 * @file    ngspice_read_dc_sweep.cpp
 * @brief   Reads the result of a DC sweep from the current plot of ngspice.
 */

#include "backends/ngspice/ngspice_read_dc_sweep.h"

#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "circuit/components/source.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include "simulation/simulation_results.h"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <string>
#include <utility>
#include <vector>

namespace Core {

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
