/**
 * @file    run_transfer_function.cpp
 * @brief   Starts a transfer function analysis.
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

void AnalysisControls::RunTransferFunctionAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.TransferFunctionOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetTransferFunction(std::nullopt);
        Finish(Analysis::TRANSFER_FUNCTION,
               sources.empty()
                   ? std::format("A transfer function analysis needs an input source; add {}", Core::SWEEPABLE_SOURCES)
                   : "Connect some parts: the transfer function needs a node to measure",
               {});
        return;
    }
    const Core::TransferFunctionSettings run_settings{
        .OutputNode = *output, .InputSource = ResolveOptionalInput(settings.TransferFunctionSource, sources)};
    StartRun(Analysis::TRANSFER_FUNCTION,
             MakeJob<Core::TransferFunctionRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunTransferFunction(copy, run_settings);
                 },
                 [this](Core::TransferFunctionRun run) {
                     Finish(Analysis::TRANSFER_FUNCTION,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTransferFunction(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
