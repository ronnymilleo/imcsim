/**
 * @file    run_pole_zero.cpp
 * @brief   Starts a pole-zero analysis.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include "simulation/analysis_sources.h"
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

void AnalysisControls::RunPoleZeroAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.PoleZeroOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetPoleZero(std::nullopt);
        Finish(Analysis::POLE_ZERO,
               sources.empty()
                   ? std::format("A pole-zero analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the pole-zero analysis needs a node to measure",
               {});
        return;
    }
    const Core::PoleZeroSettings run_settings{.OutputNode = *output,
                                              .InputSource = ResolveOptionalInput(settings.PoleZeroSource, sources)};
    StartRun(Analysis::POLE_ZERO,
             MakeJob<Core::PoleZeroRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunPoleZero(copy, run_settings);
                 },
                 [this](Core::PoleZeroRun run) {
                     Finish(Analysis::POLE_ZERO,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetPoleZero(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
