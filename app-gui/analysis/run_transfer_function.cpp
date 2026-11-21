/**
 * @file    run_transfer_function.cpp
 * @brief   Starts a transfer function analysis.
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

void AnalysisControls::RunTransferFunctionAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const auto input = ResolveSweepableInput(circuit, settings.TransferFunctionSource, "A transfer function analysis");
    const auto output =
        ResolveRequiredOutput(settings.TransferFunctionOutput, circuit, m_Schematic, "the transfer function");
    if (!input || !output) {
        Refuse(Analysis::TRANSFER_FUNCTION, !input ? input.error() : output.error());
        return;
    }
    const Core::TransferFunctionSettings run_settings{.OutputNode = *output, .InputSource = *input};
    StartSingleRun(
        Analysis::TRANSFER_FUNCTION,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &) {
            return Core::RunTransferFunction(copy, run_settings);
        },
        [this](std::optional<Core::TransferFunction> result) { m_Results.SetTransferFunction(std::move(result)); });
}

} // namespace GUI
