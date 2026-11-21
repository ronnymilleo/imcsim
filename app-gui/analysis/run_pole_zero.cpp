/**
 * @file    run_pole_zero.cpp
 * @brief   Starts a pole-zero analysis.
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

void AnalysisControls::RunPoleZeroAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const auto input = ResolveSweepableInput(circuit, settings.PoleZeroSource, "A pole-zero analysis");
    const auto output = ResolveRequiredOutput(settings.PoleZeroOutput, circuit, m_Schematic, "the pole-zero analysis");
    if (!input || !output) {
        Refuse(Analysis::POLE_ZERO, !input ? input.error() : output.error());
        return;
    }
    const Core::PoleZeroSettings run_settings{.OutputNode = *output, .InputSource = *input};
    StartSingleRun(
        Analysis::POLE_ZERO,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &) { return Core::RunPoleZero(copy, run_settings); },
        [this](std::optional<Core::PoleZero> result) { m_Results.SetPoleZero(std::move(result)); });
}

} // namespace GUI
