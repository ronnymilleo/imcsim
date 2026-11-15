/**
 * @file    run_operating_point.cpp
 * @brief   Starts an operating point, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_jobs.h"
#include "simulation/monte_carlo.h"
#include <optional>
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

} // namespace GUI
