/**
 * @file    run_sensitivity.cpp
 * @brief   Starts a sensitivity analysis, of the DC voltage or of the AC response of a node.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_starts.h"
#include "simulation/analysis_sources.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

void AnalysisControls::RunSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    if (settings.SensitivityType == SensitivityKind::AC) {
        RunACSensitivityAnalysis(circuit);
        return;
    }
    const auto output = ResolveRequiredOutput(settings.SensitivityOutput, circuit, m_Schematic, "the sensitivity");
    if (!output) {
        Refuse(Analysis::SENSITIVITY, output.error());
        return;
    }
    const Core::SensitivitySettings run_settings{.OutputNode = *output};
    StartSingleRun(
        Analysis::SENSITIVITY,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &) {
            return Core::RunSensitivity(copy, run_settings);
        },
        [this](std::optional<Core::Sensitivity> result) { m_Results.SetSensitivity(std::move(result)); });
}

void AnalysisControls::RunACSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const auto input = ResolveAcInput(circuit, settings.ACSensitivitySource, "An AC sensitivity analysis");
    const auto output = ResolveRequiredOutput(settings.SensitivityOutput, circuit, m_Schematic, "the sensitivity");
    if (!input || !output) {
        Refuse(Analysis::SENSITIVITY, !input ? input.error() : output.error());
        return;
    }
    const Core::ACSensitivitySettings run_settings{.OutputNode = *output,
                                                   .InputSource = *input,
                                                   .StartFrequency = settings.ACSensitivityRange.StartFrequency,
                                                   .StopFrequency = settings.ACSensitivityRange.StopFrequency,
                                                   .PointsPerDecade = settings.ACSensitivityRange.PointsPerDecade};
    StartSingleRun(
        Analysis::SENSITIVITY,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &) {
            return Core::RunACSensitivity(copy, run_settings);
        },
        [this](std::optional<Core::ACSensitivity> result) { m_Results.SetACSensitivity(std::move(result)); });
}

} // namespace GUI
