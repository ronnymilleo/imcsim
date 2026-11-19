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
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    if (sources.empty()) {
        Refuse(Analysis::NOISE, DescribeMissingACInput("A noise analysis"));
        return;
    }
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (!output) {
        Refuse(Analysis::NOISE, DescribeMissingOutputNode("the noise"));
        return;
    }
    const Core::NoiseSettings run_settings{.OutputNode = *output,
                                           .InputSource = ResolveOptionalInput(settings.NoiseSource, sources),
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
