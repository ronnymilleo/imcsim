/**
 * @file    run_sensitivity.cpp
 * @brief   Starts a sensitivity analysis.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunSensitivityAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
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

} // namespace GUI
