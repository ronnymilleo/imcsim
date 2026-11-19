/**
 * @file    run_starts.h
 * @brief   The AnalysisControls methods that start an analysis once, once per step of a parameter or once per Monte
 *          Carlo draw.
 * @details They are templates over the kind of result, so they live in a header, which only the run_*.cpp sources
 *          include.
 */

#ifndef IMCSIM_RUN_STARTS_H
#define IMCSIM_RUN_STARTS_H

#include "analysis/analysis_controls.h"
#include "analysis/run_jobs.h"
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace GUI {

/**
 * @brief   Starts a run made once, whose result store puts away, or whose error clears the old result.
 * @param[in] analysis  Analysis the run belongs to.
 * @param[in] circuit   Circuit to simulate; the job works on a copy of it.
 * @param[in] work      Simulates the copy on the worker thread and returns its run.
 * @param[in] store     Puts the result away, or clears it, on the thread that polls.
 */
template <typename Work, typename Store>
void AnalysisControls::StartSingleRun(const Analysis analysis, const Core::Circuit &circuit, Work work, Store store) {
    using Run = std::invoke_result_t<const Work &, const Core::Circuit &, Core::RunControl &>;
    StartRun(analysis, MakeJob<Run>(circuit, std::move(work), [this, analysis, store = std::move(store)](Run run) {
                 Finish(analysis, StoreResult(std::move(run.Result), store), std::move(run.Messages));
             }));
}

/**
 * @brief   Starts an analysis that a parameter step or a Monte Carlo run can repeat, as the repeat asks.
 * @param[in] analysis  Analysis the runs belong to.
 * @param[in] circuit   Circuit to simulate; the job works on a copy of it.
 * @param[in] repeat    A single run, the values of the step or the Monte Carlo settings.
 * @param[in] run       How to simulate once, what to measure and where the results go.
 */
template <typename Result>
void AnalysisControls::StartRepeatable(const Analysis analysis,
                                       const Core::Circuit &circuit,
                                       const RunRepeat &repeat,
                                       const RepeatableRun<Result> &run) {
    if (const auto *monte_carlo = std::get_if<Core::MonteCarloSettings>(&repeat)) {
        StartMonteCarloRuns(analysis, circuit, *monte_carlo, run);
    } else if (const auto *plan = std::get_if<StepPlan>(&repeat)) {
        StartSteppedRuns(analysis, circuit, *plan, run);
    } else {
        StartSingleRun(
            analysis,
            circuit,
            [run_once = run.RunOnce](const Core::Circuit &copy, Core::RunControl &) {
                return run_once(copy, std::nullopt);
            },
            run.StoreSingle);
    }
}

/**
 * @brief   Starts one run per value of the stepped parameter; a stop keeps none of them.
 */
template <typename Result>
void AnalysisControls::StartSteppedRuns(const Analysis analysis,
                                        const Core::Circuit &circuit,
                                        const StepPlan &plan,
                                        const RepeatableRun<Result> &run) {
    StartRun(analysis,
             MakeJob<SteppedRuns<Result>>(
                 circuit,
                 [plan, run_once = run.RunOnce](const Core::Circuit &copy, Core::RunControl &control) {
                     return RunSteps<Result>(plan.Settings,
                                             plan.Unit,
                                             plan.Values,
                                             control,
                                             [&](const Core::ParameterValue &value) { return run_once(copy, value); });
                 },
                 [this, analysis, store = run.StoreFamily](SteppedRuns<Result> runs) {
                     Finish(analysis, StoreSteps(std::move(runs.Family), store), std::move(runs.Messages));
                 }));
}

/**
 * @brief   Starts the nominal run and one run per Monte Carlo draw; a stop keeps the runs made.
 */
template <typename Result>
void AnalysisControls::StartMonteCarloRuns(const Analysis analysis,
                                           const Core::Circuit &circuit,
                                           const Core::MonteCarloSettings &monte_carlo,
                                           const RepeatableRun<Result> &run) {
    StartRun(analysis,
             MakeJob<MonteCarloRuns<Result>, KEEPS_PARTIAL_RESULT>(
                 circuit,
                 [monte_carlo, run_once = run.RunOnce, measure = run.Measure](const Core::Circuit &copy,
                                                                              Core::RunControl &control) {
                     return RunMonteCarlo<Result>(
                         monte_carlo,
                         copy,
                         control,
                         [&](const Core::Circuit &varied) { return run_once(varied, std::nullopt); },
                         measure);
                 },
                 [this, analysis, store = run.StoreFamily](MonteCarloRuns<Result> runs) {
                     Finish(analysis,
                            StoreMonteCarlo(std::move(runs.Family), std::move(runs.Table), m_Results, store),
                            std::move(runs.Messages));
                 }));
}

} // namespace GUI

#endif // IMCSIM_RUN_STARTS_H
