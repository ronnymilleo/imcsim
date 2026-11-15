/**
 * @file    run_noise.cpp
 * @brief   Starts a noise analysis.
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

void AnalysisControls::RunNoiseAnalysis(const Core::Circuit &circuit) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (sources.empty() || !output) {
        m_Results.SetNoise(std::nullopt);
        Finish(Analysis::NOISE,
               sources.empty() ? "A noise analysis needs an AC source as its input; select a voltage or current "
                                 "source and set it to AC"
                               : "Connect some parts: the noise needs a node to measure",
               {});
        return;
    }
    const Core::NoiseSettings run_settings{.OutputNode = *output,
                                           .InputSource = ResolveOptionalInput(settings.NoiseSource, sources),
                                           .StartFrequency = settings.NoiseRange.StartFrequency,
                                           .StopFrequency = settings.NoiseRange.StopFrequency,
                                           .PointsPerDecade = settings.NoiseRange.PointsPerDecade};
    StartRun(Analysis::NOISE,
             MakeJob<Core::NoiseRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunNoise(copy, run_settings);
                 },
                 [this](Core::NoiseRun run) {
                     Finish(Analysis::NOISE,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetNoise(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
