/**
 * @file    ngspice_read_sensitivity.cpp
 * @brief   Reads the result of a sensitivity analysis from the plot ngspice leaves.
 */

#include "backends/ngspice/ngspice_read_sensitivity.h"

#include "backends/ngspice/ngspice_library.h"
#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/sensitivity.h"
#include "simulation/simulation_results.h"
#include <cmath>
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>

namespace Core {

namespace {

/**
 * @brief   The plot a sensitivity analysis leaves; its name starts with "sens".
 */
std::optional<std::string> FindSensitivityPlot() {
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with("sens")) {
            return name;
        }
    }
    return std::nullopt;
}

/**
 * @brief   The first value of every vector of the plot that has a finite one, by name.
 * @details ngspice also saves the parameters of each model under "device:parameter", which are left out, and reports
 *          some as nan when the model does not use them.
 */
SensitivityVectors ReadSensitivityVectors(const std::string &plot) {
    SensitivityVectors values;
    for (const std::string &name : ListVectors(plot)) {
        if (name.contains(':')) {
            continue;
        }
        const pvector_info vector = FindVector(name);
        if (vector == nullptr) {
            continue;
        }
        const double value = RealPart(*vector).front();
        if (std::isfinite(value)) {
            values[name] = value;
        }
    }
    return values;
}

} // namespace

/**
 * @brief   Reads the sensitivity of every part to the voltage of the output node.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node the run used.
 * @return  The sensitivity of every part with a value, or the error when ngspice left no sensitivity plot.
 */
std::expected<Sensitivity, std::string> ReadSensitivity(const Circuit &circuit, const SensitivitySettings &settings) {
    const std::optional<std::string> plot = FindSensitivityPlot();
    if (!plot) {
        return std::unexpected("ngspice returned no sensitivity plot");
    }
    SendCommand(std::format("setplot {}", *plot));
    return BuildSensitivity(circuit, settings.OutputNode, ReadSensitivityVectors(*plot));
}

} // namespace Core
