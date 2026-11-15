/**
 * @file    run_transient.cpp
 * @brief   Starts a transient, live or not, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_jobs.h"
#include "simulation/check_transient.h"
#include "simulation/monte_carlo.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace GUI {

/**
 * @brief   A live run is a transient that goes on to its limit at the pace the settings give, unless the user stops it
 *          first.
 */
void AnalysisControls::RunTransientAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                            const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    const Core::TransientSettings settings = m_Schematic.GetSimulationSettings().Transient;
    m_IsLiveResult = false;
    if (monte_carlo) {
        StartRun(Analysis::TRANSIENT,
                 MakeJob<MonteCarloRuns<Core::Transient>, true>(
                     circuit,
                     [settings, monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::Transient>(
                             monte_carlo, copy, control,
                             [&](const Core::Circuit &varied) { return Core::RunTransient(varied, settings); },
                             MeasureTransient);
                     },
                     [this](MonteCarloRuns<Core::Transient> runs) {
                         Finish(
                             Analysis::TRANSIENT,
                             StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Results,
                                             [this](auto family) { m_Results.SetTransientSteps(std::move(family)); }),
                             std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::TRANSIENT,
                 MakeJob<SteppedRuns<Core::Transient>>(
                     circuit,
                     [settings, plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::Transient>(plan.Settings, plan.Unit, plan.Values, control,
                                                          [&](const Core::ParameterValue &value) {
                                                              return Core::RunTransient(copy, settings, value);
                                                          });
                     },
                     [this](SteppedRuns<Core::Transient> runs) {
                         Finish(Analysis::TRANSIENT,
                                StoreSteps(std::move(runs.Family),
                                           [this](auto family) { m_Results.SetTransientSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    // The points arrive in chunks while the run goes on, so the old result gives way to the first one
    m_Results.SetTransient(std::nullopt);
    const std::size_t circuit_version = m_Schematic.GetCircuitVersion();
    m_IsLiveRun = m_IsLiveModeOn && !FindLiveObstacle();
    m_IsLiveResult = m_IsLiveRun;
    m_Schematic.SetLiveRun(m_IsLiveRun);
    Core::TransientSettings run_settings = settings;
    double pace = 0.0;
    if (m_IsLiveRun) {
        pace = settings.StopTime / m_Schematic.GetSimulationSettings().LiveSeconds;
        // One step short of the most a transient may span, which the check would refuse after rounding
        // A noise or random source steps the transient on its own, so the finest step counts for the points
        const double finest_step =
            std::min(settings.TimeStep, Core::FindFinestSignalStep(circuit).value_or(settings.TimeStep));
        run_settings.StopTime =
            std::min(pace * LIVE_RUN_LIMIT_SECONDS, finest_step * static_cast<double>(Core::MAX_TRANSIENT_POINTS - 1));
    }
    const bool is_live = m_IsLiveRun;
    StartRun(Analysis::TRANSIENT,
             MakeJob<Core::TransientRun, true>(
                 circuit,
                 [this, run_settings, circuit_version, pace](const Core::Circuit &copy, Core::RunControl &control) {
                     const auto post_chunk = [this, &control, circuit_version](Core::Transient chunk) {
                         control.Post(
                             [this, circuit_version, points = std::make_shared<Core::Transient>(std::move(chunk))] {
                                 if (m_Schematic.GetCircuitVersion() == circuit_version) {
                                     m_Results.AppendTransient(std::move(*points));
                                 }
                             });
                     };
                     return Core::RunTransient(copy, run_settings, std::nullopt,
                                               {.Control = &control, .OnChunk = post_chunk, .LivePace = pace});
                 },
                 [this, is_live](Core::TransientRun run) {
                     if (is_live && run.Result && run.Result->IsComplete) {
                         run.Result->Warnings.emplace_back(
                             "The live run reached its limit, an hour on the clock or a million time steps, whichever "
                             "comes first; Run starts a new one");
                     }
                     Finish(Analysis::TRANSIENT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetTransient(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

} // namespace GUI
