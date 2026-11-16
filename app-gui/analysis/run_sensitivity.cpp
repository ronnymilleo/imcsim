/**
 * @file    run_sensitivity.cpp
 * @brief   Starts a sensitivity analysis, of the DC voltage or of the AC response of a node.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
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
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (!output) {
        m_Results.SetSensitivity(std::nullopt);
        Finish(Analysis::SENSITIVITY, "Connect some parts: the sensitivity needs a node to measure", {});
        return;
    }
    const Core::SensitivitySettings run_settings{.OutputNode = *output};
    StartRun(Analysis::SENSITIVITY,
             MakeJob<Core::SensitivityRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunSensitivity(copy, run_settings);
                 },
                 [this](Core::SensitivityRun run) {
                     Finish(Analysis::SENSITIVITY,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetSensitivity(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

void AnalysisControls::RunACSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetACSensitivity(std::nullopt);
        Finish(Analysis::SENSITIVITY,
               sources.empty() ? "An AC sensitivity analysis needs an AC source as its input; select a voltage or "
                                 "current source and set it to AC"
                               : "Connect some parts: the sensitivity needs a node to measure",
               {});
        return;
    }
    const Core::ACSensitivitySettings run_settings{.OutputNode = *output,
                                                   .InputSource =
                                                       ResolveOptionalInput(settings.ACSensitivitySource, sources),
                                                   .StartFrequency = settings.ACSensitivityRange.StartFrequency,
                                                   .StopFrequency = settings.ACSensitivityRange.StopFrequency,
                                                   .PointsPerDecade = settings.ACSensitivityRange.PointsPerDecade};
    StartRun(Analysis::SENSITIVITY,
             MakeJob<Core::ACSensitivityRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunACSensitivity(copy, run_settings);
                 },
                 [this](Core::ACSensitivityRun run) {
                     Finish(Analysis::SENSITIVITY,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetACSensitivity(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
