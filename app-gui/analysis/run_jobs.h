/**
 * @file    run_jobs.h
 * @brief   Building blocks that put an analysis on the worker thread: the job that simulates a copy of the circuit,
 *          the runs once per step or per Monte Carlo draw, and the hand-over of what they make to the results.
 * @details These are templates over the kind of result, so they live in a header; AnalysisControls is their only user.
 */

#ifndef IMCSIM_RUN_JOBS_H
#define IMCSIM_RUN_JOBS_H

#include "results/monte_carlo_results.h"
#include "results/parameter_step.h"
#include "results/result_store.h"
#include "schematic/parameter_step_settings.h"
#include "simulation/circuit_copy.h"
#include "simulation/monte_carlo.h"
#include "simulation/parallel_runs.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulation_service.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

/// A job that is stopped still delivers what it made, as the points of a transient or the runs of a Monte Carlo
/// analysis.
constexpr bool KEEPS_PARTIAL_RESULT = true;
/// Runs whose traces are kept besides the nominal one; the rest only leave their measurements, since the traces of a
/// thousand transients would not fit in memory.
constexpr std::size_t MAX_KEPT_TRACES = 100;

/**
 * @struct  MonteCarloDrawOutcome
 * @brief   What one Monte Carlo draw produces before its trace and measurements are folded into the table in run order.
 */
template <typename Result>
struct MonteCarloDrawOutcome {
    std::vector<double> Values;
    std::expected<Result, std::string> Outcome;
    std::optional<Measurement> Measured;
    std::vector<Core::SimulatorMessage> Messages;
};

/**
 * @struct  MonteCarloDraws
 * @brief   What the runs of a Monte Carlo analysis have left so far: the traces kept and the table of every run.
 */
template <typename Result>
struct MonteCarloDraws {
    StepFamily<Result> Family;
    MonteCarloTable Table;
};

/**
 * @struct  MonteCarloRuns
 * @brief   The traces kept from the runs of a Monte Carlo analysis, or why it could not run, what every run measured
 *          and what ngspice printed in all.
 */
template <typename Result>
struct MonteCarloRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    MonteCarloTable Table;
    std::vector<Core::SimulatorMessage> Messages;
};

/**
 * @struct  RepeatableRun
 * @brief   What an analysis that a parameter step or a Monte Carlo run can repeat gives the run: how to simulate the
 *          circuit once, what to measure in each draw, and where the results go.
 */
template <typename Result>
struct RepeatableRun {
    /// Simulates the circuit once, with the stepped parameter at a value, or as it is when there is none.
    std::function<Core::SimulationRun<Result>(const Core::Circuit &, const std::optional<Core::ParameterValue> &)>
        RunOnce;
    /// What the table of a Monte Carlo analysis keeps from each draw.
    std::function<Measurement(const Result &)> Measure;
    /// Stores the result of a single run, or clears it when the run failed.
    std::function<void(std::optional<Result>)> StoreSingle;
    /// Stores the results of the steps, or the runs a Monte Carlo analysis kept.
    std::function<void(StepFamily<Result>)> StoreFamily;
};

/**
 * @struct  SteppedRuns
 * @brief   The results of an analysis run once per step, or why a step failed, and what ngspice printed in all.
 */
template <typename Result>
struct SteppedRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    std::vector<Core::SimulatorMessage> Messages;
};

/**
 * @brief   Hands a result to the results, or clears the old one so stale values never sit next to an error.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreResult(std::expected<Result, std::string> result, Store store) {
    if (result) {
        store(std::optional<Result>(std::move(*result)));
        return std::nullopt;
    }
    store(std::optional<Result>());
    return std::move(result.error());
}

/**
 * @brief   Runs once per value and stops at the first step that fails or when asked to; the output of each step is
 *          headed by its value, so a warning can be traced to the step that printed it.
 */
template <typename Result, typename RunStep>
SteppedRuns<Result> RunSteps(const ParameterStepSettings &step,
                             const std::string &unit,
                             const std::vector<double> &values,
                             Core::RunControl &control,
                             const RunStep &run_step) {
    StepFamily<Result> family{.Parameter = GetParameterLabel(step), .Unit = unit, .Values = values, .Results = {}};
    std::vector<std::optional<Core::SimulationRun<Result>>> outcomes(values.size());
    const bool is_concurrent = Core::GetBackend().GetCapabilities().SupportsConcurrentRuns;

    Core::RunBatch(values.size(), is_concurrent, &control, [&](const std::size_t index) {
        Core::SimulationRun<Result> run = run_step(Core::ParameterValue{step.Part, values[index]});
        const bool succeeded = run.Result.has_value();
        outcomes[index] = std::move(run);
        return succeeded;
    });

    std::vector<Core::SimulatorMessage> messages;
    family.Results.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        std::optional<Core::SimulationRun<Result>> &slot = outcomes[index];
        if (!slot) {
            return {std::unexpected("Stopped"), std::move(messages)};
        }
        Core::SimulationRun<Result> &run = *slot;
        const std::string label = FormatStepValue(family.Parameter, values[index], unit);
        messages.push_back({std::format("Step {}", label), false});
        std::ranges::move(run.Messages, std::back_inserter(messages));
        if (!run.Result) {
            return {std::unexpected(std::format("At {}: {}", label, run.Result.error())), std::move(messages)};
        }
        family.Results.push_back(std::move(*run.Result));
    }
    if (control.IsCancelled()) {
        return {std::unexpected("Stopped"), std::move(messages)};
    }
    return {std::move(family), std::move(messages)};
}

MonteCarloTable MakeMonteCarloTable(const Core::Circuit &circuit, const Core::MonteCarloSettings &settings);

/**
 * @brief   Tells whether the next run that converges keeps its trace, which only the first runs do.
 * @param[in] family  Traces kept so far, the nominal one first.
 * @return  True while the family holds no more than MAX_KEPT_TRACES traces besides the nominal one.
 */
template <typename Result>
bool HasRoomForTrace(const StepFamily<Result> &family) {
    return family.Results.size() <= MAX_KEPT_TRACES;
}

/**
 * @brief   Adds one run to the table, with what it measured or why it failed, and its trace to the family while there
 *          is room; the nominal run also names the columns of the table.
 * @param[in,out] draws    What the runs have left so far.
 * @param[in]     run      Number of the run, 0 for the nominal one.
 * @param[in]     values   Values the run gave the varied parts.
 * @param[in]     result   What the run made, or why it failed.
 * @param[in]     measure  What the table keeps from a run that converged.
 */
template <typename Result, typename Measure>
void RecordDraw(MonteCarloDraws<Result> &draws,
                const int run,
                std::vector<double> values,
                std::expected<Result, std::string> result,
                const Measure &measure) {
    MonteCarloRun entry{.Values = std::move(values)};
    if (!result) {
        entry.IsFailed = true;
        entry.Error = std::move(result.error());
    } else {
        Measurement measured = measure(*result);
        if (run == 0) {
            draws.Table.Columns = std::move(measured.Columns);
        }
        entry.Measures = std::move(measured.Values);
        if (HasRoomForTrace(draws.Family)) {
            draws.Family.Values.push_back(static_cast<double>(run));
            draws.Family.Results.push_back(std::move(*result));
        }
    }
    draws.Table.Runs.push_back(std::move(entry));
}

/**
 * @brief   Runs one Monte Carlo draw and measures its output before dropping traces past MAX_KEPT_TRACES.
 */
template <typename Result, typename RunCircuit, typename Measure>
MonteCarloDrawOutcome<Result> RunOneMonteCarloDraw(const Core::MonteCarloSettings &settings,
                                                   const Core::Circuit &circuit,
                                                   const int run,
                                                   const RunCircuit &run_circuit,
                                                   const Measure &measure) {
    Core::MonteCarloCircuit built = Core::BuildMonteCarloCircuit(circuit, settings, run);
    Core::SimulationRun<Result> simulated = run_circuit(built.Simulated);
    MonteCarloDrawOutcome<Result> outcome{
        .Values = std::move(built.Values),
        .Outcome = std::move(simulated.Result),
        .Measured = std::nullopt,
        .Messages = std::move(simulated.Messages),
    };
    if (outcome.Outcome) {
        outcome.Measured = measure(*outcome.Outcome);
        if (static_cast<std::size_t>(run) > MAX_KEPT_TRACES) {
            outcome.Outcome = Result{};
        }
    }
    return outcome;
}

/**
 * @brief   Runs the nominal circuit and then each draw, and goes on past a run that fails to converge, since a random
 *          draw may land on values the solver cannot handle; the nominal run has to work.
 * @details A stop keeps the runs made. Only the first runs keep their traces; every run keeps its measurements.
 */
template <typename Result, typename RunCircuit, typename Measure>
MonteCarloRuns<Result> RunMonteCarlo(const Core::MonteCarloSettings &settings,
                                     const Core::Circuit &circuit,
                                     Core::RunControl &control,
                                     const RunCircuit &run_circuit,
                                     const Measure &measure) {
    MonteCarloDraws<Result> draws{
        .Family = {.Parameter = MONTE_CARLO_PARAMETER, .Unit = "", .Values = {}, .Results = {}},
        .Table = MakeMonteCarloTable(circuit, settings)};
    std::vector<Core::SimulatorMessage> messages;
    if (control.IsCancelled()) {
        return {std::unexpected("Stopped"), {}, std::move(messages)};
    }

    const std::size_t total_runs = static_cast<std::size_t>(settings.Runs) + 1;
    control.SetProgress(0.0);
    Core::MonteCarloCircuit nominal = Core::BuildMonteCarloCircuit(circuit, settings, 0);
    Core::SimulationRun<Result> nominal_outcome = run_circuit(nominal.Simulated);
    messages.push_back({std::string("Nominal run"), false});
    std::ranges::move(nominal_outcome.Messages, std::back_inserter(messages));
    if (!nominal_outcome.Result) {
        return {std::unexpected(std::format("The nominal run failed: {}", nominal_outcome.Result.error())),
                {},
                std::move(messages)};
    }
    RecordDraw(draws, 0, std::move(nominal.Values), std::move(nominal_outcome.Result), measure);

    const std::size_t draw_count = static_cast<std::size_t>(settings.Runs);
    std::vector<std::optional<MonteCarloDrawOutcome<Result>>> outcomes(draw_count);
    const bool is_concurrent = Core::GetBackend().GetCapabilities().SupportsConcurrentRuns;

    Core::RunBatch(
        draw_count,
        is_concurrent,
        &control,
        [&](const std::size_t draw_index) {
            const int run = static_cast<int>(draw_index + 1);
            outcomes[draw_index] = RunOneMonteCarloDraw<Result>(settings, circuit, run, run_circuit, measure);
            return true;
        },
        1,
        total_runs);

    for (std::size_t draw_index = 0; draw_index < draw_count; ++draw_index) {
        std::optional<MonteCarloDrawOutcome<Result>> &slot = outcomes[draw_index];
        if (!slot) {
            break;
        }
        const int run = static_cast<int>(draw_index + 1);
        MonteCarloDrawOutcome<Result> &item = *slot;
        messages.push_back({std::format("Run {}", run), false});
        std::ranges::move(item.Messages, std::back_inserter(messages));
        MonteCarloRun entry{.Values = std::move(item.Values)};
        if (!item.Outcome) {
            entry.IsFailed = true;
            entry.Error = std::move(item.Outcome.error());
        } else {
            if (item.Measured) {
                entry.Measures = std::move(item.Measured->Values);
            }
            if (HasRoomForTrace(draws.Family)) {
                draws.Family.Values.push_back(static_cast<double>(run));
                draws.Family.Results.push_back(std::move(*item.Outcome));
            }
        }
        draws.Table.Runs.push_back(std::move(entry));
    }
    return {std::move(draws.Family), std::move(draws.Table), std::move(messages)};
}

/**
 * @brief   Hands the steps to the results, or reports the step that failed; the old result is cleared before the
 *          steps run.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreSteps(std::expected<StepFamily<Result>, std::string> family, Store store) {
    if (!family) {
        return std::move(family.error());
    }
    store(std::move(*family));
    return std::nullopt;
}

/**
 * @brief   Hands the traces of the runs to the results as StoreSteps() does, and then their table, or reports why they
 *          did not run.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreMonteCarlo(std::expected<StepFamily<Result>, std::string> family,
                                           MonteCarloTable table,
                                           ResultStore &results,
                                           Store store) {
    std::optional<std::string> error = StoreSteps(std::move(family), std::move(store));
    if (!error) {
        results.SetMonteCarlo(std::move(table));
    }
    return error;
}

/**
 * @brief   A job that simulates its own copy of the circuit, so the user can go on editing, and hands the run it makes
 *          to the thread that polls, where store puts it away.
 * @details A job that was stopped delivers nothing, unless what it has made is worth keeping, as the points of a
 *          transient.
 */
template <typename Run, bool KeepsPartial = false, typename Work, typename Store>
Core::Job MakeJob(const Core::Circuit &circuit, Work work, Store store) {
    const auto copy = std::make_shared<const Core::CircuitCopy>(circuit);
    return [copy, work = std::move(work), store = std::move(store)](Core::RunControl &control) -> Core::Delivery {
        auto run = std::make_shared<Run>(work(copy->GetCircuit(), control));
        if (control.IsCancelled() && !KeepsPartial) {
            return {};
        }
        return [run, store] { store(std::move(*run)); };
    };
}

} // namespace GUI

#endif // IMCSIM_RUN_JOBS_H
