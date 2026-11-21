/**
 * @file    run_repeat.cpp
 * @brief   How a run of an analysis repeats: once, once per value of a stepped parameter, or once per Monte Carlo draw.
 */

#include "analysis/run_repeat.h"

#include "analysis/analysis_inputs.h"
#include <optional>
#include <utility>

namespace GUI {

/**
 * @brief   Tells how the selected analysis repeats, from the step and Monte Carlo settings.
 * @param[in] settings  Settings of the schematic; Monte Carlo wins when both it and the step are on.
 * @param[in] circuit   Circuit the analysis runs on, which gives the unit of the stepped part and its tolerances.
 * @return  A single run for an analysis that never repeats, such as the noise, or when neither is on; the values of the
 *          step or the Monte Carlo settings otherwise; the reason when the step has no valid values or no part of the
 *          circuit has a tolerance to draw from.
 */
std::expected<RunRepeat, std::string> PlanRunRepeat(const SimulationSettings &settings, const Core::Circuit &circuit) {
    if (!IsRepeatable(settings.Selected)) {
        return std::monostate{};
    }
    if (settings.MonteCarlo.Enabled) {
        if (std::optional<std::string> problem = Core::CheckMonteCarlo(circuit, settings.MonteCarlo)) {
            return std::unexpected(std::move(*problem));
        }
        return settings.MonteCarlo;
    }
    if (!settings.ParameterStep.Enabled) {
        return std::monostate{};
    }
    std::string unit = GetParameterUnit(circuit, settings.ParameterStep.Part);
    auto values = ListStepValues(settings.ParameterStep, unit);
    if (!values) {
        return std::unexpected(std::move(values.error()));
    }
    return StepPlan{.Settings = settings.ParameterStep, .Unit = std::move(unit), .Values = std::move(*values)};
}

/**
 * @brief   Tells whether an analysis runs once.
 * @param[in] repeat  How the analysis repeats.
 * @return  True when it is neither stepped nor a Monte Carlo analysis.
 */
bool IsSingleRun(const RunRepeat &repeat) {
    return std::holds_alternative<std::monostate>(repeat);
}

} // namespace GUI
