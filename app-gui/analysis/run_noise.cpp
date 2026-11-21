/**
 * @file    run_noise.cpp
 * @brief   Starts a noise analysis.
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

void AnalysisControls::RunNoiseAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const auto input = ResolveAcInput(circuit, settings.NoiseSource, "A noise analysis");
    const auto output = ResolveRequiredOutput(settings.NoiseOutput, circuit, m_Schematic, "the noise");
    if (!input || !output) {
        Refuse(Analysis::NOISE, !input ? input.error() : output.error());
        return;
    }
    const Core::NoiseSettings run_settings{.OutputNode = *output,
                                           .InputSource = *input,
                                           .StartFrequency = settings.NoiseRange.StartFrequency,
                                           .StopFrequency = settings.NoiseRange.StopFrequency,
                                           .PointsPerDecade = settings.NoiseRange.PointsPerDecade};
    StartSingleRun(
        Analysis::NOISE,
        circuit,
        [run_settings](const Core::Circuit &copy, Core::RunControl &) { return Core::RunNoise(copy, run_settings); },
        [this](std::optional<Core::Noise> result) { m_Results.SetNoise(std::move(result)); });
}

} // namespace GUI
