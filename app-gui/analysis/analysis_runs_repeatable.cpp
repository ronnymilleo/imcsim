/**
 * @file    analysis_runs_repeatable.cpp
 * @brief   Starts the analyses that a parameter step or a Monte Carlo run repeats: the operating point, the transient
 *          (live runs included), the AC sweep and the DC sweep.
 */

#include "analysis/analysis_controls.h"

#include "analysis/analysis_inputs.h"
#include "analysis/run_jobs.h"
#include "simulation/circuit_checks.h"
#include "simulation/monte_carlo.h"
#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace GUI {

/**
 * @brief   A stepped or Monte Carlo operating point keeps every step, and the schematic shows the selected one.
 */
void AnalysisControls::RunOperatingPointAnalysis(const Core::Circuit &circuit, const std::optional<StepPlan> &steps,
                                                 const std::optional<Core::MonteCarloSettings> &monte_carlo) {
    if (monte_carlo) {
        StartRun(Analysis::OPERATING_POINT,
                 MakeJob<MonteCarloRuns<Core::OperatingPoint>, true>(
                     circuit,
                     [monte_carlo = *monte_carlo](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunMonteCarlo<Core::OperatingPoint>(
                             monte_carlo, copy, control,
                             [](const Core::Circuit &varied) { return Core::RunOperatingPoint(varied); },
                             MeasureOperatingPoint);
                     },
                     [this](MonteCarloRuns<Core::OperatingPoint> runs) {
                         Finish(Analysis::OPERATING_POINT,
                                StoreMonteCarlo(
                                    std::move(runs.Family), std::move(runs.Table), m_Results,
                                    [this](auto family) { m_Results.SetOperatingPointSteps(std::move(family)); }),
                                std::move(runs.Messages));
                     }));
        return;
    }
    if (steps) {
        StartRun(Analysis::OPERATING_POINT,
                 MakeJob<SteppedRuns<Core::OperatingPoint>>(
                     circuit,
                     [plan = *steps](const Core::Circuit &copy, Core::RunControl &control) {
                         return RunSteps<Core::OperatingPoint>(
                             plan.Settings, plan.Unit, plan.Values, control,
                             [&](const Core::ParameterValue &value) { return Core::RunOperatingPoint(copy, value); });
                     },
                     [this](SteppedRuns<Core::OperatingPoint> runs) {
                         Finish(
                             Analysis::OPERATING_POINT,
                             StoreSteps(std::move(runs.Family),
                                        [this](auto family) { m_Results.SetOperatingPointSteps(std::move(family)); }),
                             std::move(runs.Messages));
                     }));
        return;
    }
    StartRun(Analysis::OPERATING_POINT,
             MakeJob<Core::OperatingPointRun>(
                 circuit, [](const Core::Circuit &copy, Core::RunControl &) { return Core::RunOperatingPoint(copy); },
                 [this](Core::OperatingPointRun run) {
                     Finish(Analysis::OPERATING_POINT,
                            StoreResult(std::move(run.Result),
                                        [this](auto result) { m_Results.SetOperatingPoint(std::move(result)); }),
                            std::move(run.Messages));
                 }));
}

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
