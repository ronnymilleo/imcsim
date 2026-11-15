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
#include "simulation/simulation_service.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

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
 * @struct  SteppedRuns
 * @brief   The results of an analysis run once per step, or why a step failed, and what ngspice printed in all.
 */
template <typename Result> struct SteppedRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    std::vector<Core::SimulatorMessage> Messages;
};

/**
 * @brief   Runs once per value and stops at the first step that fails or when asked to; the output of each step is
 *          headed by its value, so a warning can be traced to the step that printed it.
 */
template <typename Result, typename RunStep>
SteppedRuns<Result> RunSteps(const ParameterStepSettings &step, const std::string &unit,
                             const std::vector<double> &values, Core::RunControl &control, const RunStep &run_step) {
    StepFamily<Result> family{.Parameter = GetParameterLabel(step), .Unit = unit, .Values = values, .Results = {}};
    std::vector<Core::SimulatorMessage> messages;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (control.IsCancelled()) {
            return {std::unexpected("Stopped"), std::move(messages)};
        }
        control.SetProgress(static_cast<double>(index) / static_cast<double>(values.size()));
        const std::string label = FormatStepValue(family.Parameter, values[index], unit);
        auto run = run_step(Core::ParameterValue{step.Part, values[index]});
        messages.push_back({std::format("Step {}", label), false});
        std::ranges::move(run.Messages, std::back_inserter(messages));
        if (!run.Result) {
            return {std::unexpected(std::format("At {}: {}", label, run.Result.error())), std::move(messages)};
        }
        family.Results.push_back(std::move(*run.Result));
    }
    return {std::move(family), std::move(messages)};
}

/**
 * @struct  MonteCarloRuns
 * @brief   The traces kept from the runs of a Monte Carlo analysis, or why it could not run, what every run measured
 *          and what ngspice printed in all.
 */
template <typename Result> struct MonteCarloRuns {
    std::expected<StepFamily<Result>, std::string> Family;
    MonteCarloTable Table;
    std::vector<Core::SimulatorMessage> Messages;
};

/// Runs whose traces are kept besides the nominal one; the rest only leave their measurements, since the traces of a
/// thousand transients would not fit in memory.
constexpr std::size_t MAX_KEPT_TRACES = 100;

MonteCarloTable MakeMonteCarloTable(const Core::Circuit &circuit, const Core::MonteCarloSettings &settings);

/**
 * @brief   Runs the nominal circuit and then each draw, and goes on past a run that fails to converge, since a random
 *          draw may land on values the solver cannot handle; the nominal run has to work.
 * @details A stop keeps the runs made. Only the first runs keep their traces; every run keeps its measurements.
 */
template <typename Result, typename RunCircuit, typename Measure>
MonteCarloRuns<Result> RunMonteCarlo(const Core::MonteCarloSettings &settings, const Core::Circuit &circuit,
                                     Core::RunControl &control, const RunCircuit &run_circuit, const Measure &measure) {
    StepFamily<Result> family{.Parameter = MONTE_CARLO_PARAMETER, .Unit = "", .Values = {}, .Results = {}};
    MonteCarloTable table = MakeMonteCarloTable(circuit, settings);
    std::vector<Core::SimulatorMessage> messages;
    for (int run = 0; run <= settings.Runs && !control.IsCancelled(); ++run) {
        control.SetProgress(static_cast<double>(run) / static_cast<double>(settings.Runs + 1));
        const Core::MonteCarloCircuit built = Core::BuildMonteCarloCircuit(circuit, settings, run);
        auto outcome = run_circuit(built.Simulated);
        messages.push_back({run == 0 ? std::string("Nominal run") : std::format("Run {}", run), false});
        std::ranges::move(outcome.Messages, std::back_inserter(messages));
        MonteCarloRun entry{.Values = built.Values};
        if (!outcome.Result) {
            if (run == 0) {
                return {std::unexpected(std::format("The nominal run failed: {}", outcome.Result.error())),
                        {},
                        std::move(messages)};
            }
            entry.IsFailed = true;
            entry.Error = std::move(outcome.Result.error());
        } else {
            Measurement measured = measure(*outcome.Result);
            if (run == 0) {
                table.Columns = std::move(measured.Columns);
            }
            entry.Measures = std::move(measured.Values);
            if (family.Results.size() <= MAX_KEPT_TRACES) {
                family.Values.push_back(static_cast<double>(run));
                family.Results.push_back(std::move(*outcome.Result));
            }
        }
        table.Runs.push_back(std::move(entry));
    }
    if (table.Runs.empty()) {
        return {std::unexpected("Stopped"), {}, std::move(messages)};
    }
    return {std::move(family), std::move(table), std::move(messages)};
}

/**
 * @brief   Hands the traces and the table of the runs to the results, or reports why they did not run.
 */
template <typename Result, typename Store>
std::optional<std::string> StoreMonteCarlo(std::expected<StepFamily<Result>, std::string> family, MonteCarloTable table,
                                           ResultStore &results, Store store) {
    if (!family) {
        return std::move(family.error());
    }
    store(std::move(*family));
    results.SetMonteCarlo(std::move(table));
    return std::nullopt;
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

} // namespace GUI

#endif // IMCSIM_RUN_JOBS_H
