/**
 * @file    run_dc_sweep.cpp
 * @brief   Starts a DC sweep, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include "simulation/analysis_sources.h"
#include "simulation/monte_carlo.h"
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace GUI {

/**
 * @brief   A source that is unset, or that left the circuit, uses a default; a stepped parameter and a second stepped
 *          source would both make the family of curves, so only one may be on.
 */
void AnalysisControls::RunDCSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                          const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const std::vector<SweepSource> sources = ListSweepSources(circuit);
    if (sources.empty() || ((steps || monte_carlo) && settings.StepSource)) {
        m_Results.SetDCSweep(std::nullopt);
        Finish(Analysis::DC_SWEEP,
               sources.empty() ? std::format("Add {} to sweep", Core::SWEEPABLE_SOURCES)
               : monte_carlo   ? "Monte Carlo makes one curve per run, so turn off the second stepped source"
                               : "Step either a second source or a parameter, not both",
               {});
        return;
    }
    Core::DCSweepSettings run_settings{.Swept = settings.SweptRange};
    run_settings.Swept.Source = ResolveSweepSource(settings.SweptRange, sources, SWEPT_SOURCE_INDEX);
    if (settings.StepSource) {
        run_settings.Stepped = settings.SteppedRange;
        run_settings.Stepped->Source = ResolveSweepSource(settings.SteppedRange, sources, STEPPED_SOURCE_INDEX);
    }
    if (monte_carlo) {
        StartRun(Analysis::DC_SWEEP,
                 MakeJob<MonteCarloRuns<Core::DCSweep>, true>(
                     circuit,
                     [run_settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::DCSweep>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunDCSweep(varied, run_settings); },
                             MeasureDCSweep);
                     },
                     [this](MonteCarloRuns<Core::DCSweep> runs) {
                         Finish(Analysis::DC_SWEEP,
                                StoreMonteCarlo(
                                    std::move(runs.Family), std::move(runs.Table), m_Results,
                                    [this](auto family) { m_Results.SetDCSweep(JoinDCSweepSteps(std::move(family))); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::DC_SWEEP,
                 MakeJob<SteppedRuns<Core::DCSweep>>(
                     circuit,
                     [run_settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::DCSweep>(plan.Settings, plan.Unit, plan.Values, control,
                                                        [&](const Core::ParameterValue &value) {
                                                            return Core::RunDCSweep(copy, run_settings, value);
                                                        });
                     },
                     [this](SteppedRuns<Core::DCSweep> runs) {
                         Finish(Analysis::DC_SWEEP,
                                StoreSteps(
                                    std::move(runs.Family),
                                    [this](auto family) { m_Results.SetDCSweep(JoinDCSweepSteps(std::move(family))); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::DC_SWEEP,
             MakeJob<Core::DCSweepRun>(
                 circuit,
                 [run_settings](const Core::Circuit &copy, Core::RunControl &) {
                     return Core::RunDCSweep(copy, run_settings);
                 },
                 [this](Core::DCSweepRun run) {
                     Finish(Analysis::DC_SWEEP,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetDCSweep(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
