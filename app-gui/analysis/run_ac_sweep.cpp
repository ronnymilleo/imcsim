/**
 * @file    run_ac_sweep.cpp
 * @brief   Starts an AC sweep, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_jobs.h"
#include "simulation/monte_carlo.h"
#include <optional>
#include <utility>

namespace GUI {

void AnalysisControls::RunACSweepAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                          const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const Core::ACSweepSettings settings = m_Schematic.GetSimulationSettings().ACSweep;
    if (monte_carlo) {
        StartRun(Analysis::AC_SWEEP,
                 MakeJob<MonteCarloRuns<Core::ACSweep>, true>(
                     circuit,
                     [settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::ACSweep>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunACSweep(varied, settings); },
                             MeasureACSweep);
                     },
                     [this](MonteCarloRuns<Core::ACSweep> runs) {
                         Finish(Analysis::AC_SWEEP,
                                StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Results,
                                                [this](auto family) { m_Results.SetACSweepSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::AC_SWEEP,
                 MakeJob<SteppedRuns<Core::ACSweep>>(
                     circuit,
                     [settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::ACSweep>(plan.Settings, plan.Unit, plan.Values, control,
                                                        [&](const Core::ParameterValue &value) {
                                                            return Core::RunACSweep(copy, settings, value);
                                                        });
                     },
                     [this](SteppedRuns<Core::ACSweep> runs) {
                         Finish(Analysis::AC_SWEEP,
                                StoreSteps(std::move(runs.Family),
                                           [this](auto family) { m_Results.SetACSweepSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::AC_SWEEP,
             MakeJob<Core::ACSweepRun>(
                 circuit,
                 [settings](const Core::Circuit &copy, Core::RunControl &) { return Core::RunACSweep(copy, settings); },
                 [this](Core::ACSweepRun run) {
                     Finish(Analysis::AC_SWEEP,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetACSweep(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
