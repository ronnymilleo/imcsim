/**
 * @file    run_jobs_tests.cpp
 * @brief   Tests for the building blocks that put an analysis on the worker thread: what the runs of a Monte Carlo
 *          analysis keep, and how their results are stored.
 */

#include "analysis/run_jobs.h"

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/vcc.h"
#include "nodalys_in_use.h"
#include "results/monte_carlo_results.h"
#include "results/parameter_step.h"
#include "results/result_store.h"
#include "simulation/simulation_backend.h"
#include "test_printers.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

/**
 * @brief   A run of these tests makes a number, which it measures as the mean of node 1.
 */
GUI::Measurement MeasureNumber(const double number) {
    return {.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::MEAN, .Current = ""}}, .Values = {number}};
}

GUI::MonteCarloDraws<double> MakeDraws() {
    return {.Family = {.Parameter = GUI::MONTE_CARLO_PARAMETER, .Unit = "", .Values = {}, .Results = {}}, .Table = {}};
}

} // namespace

TEST_CASE("Traces are kept for the nominal run and the first hundred others", "[run_jobs]") {
    GUI::StepFamily<double> family;
    family.Results.resize(GUI::MAX_KEPT_TRACES);
    CHECK(GUI::HasRoomForTrace(family));
    family.Results.push_back(0.0);
    CHECK_FALSE(GUI::HasRoomForTrace(family));
}

TEST_CASE("A run that converged leaves its measurements and its trace, and the nominal one names the columns",
          "[run_jobs]") {
    GUI::MonteCarloDraws<double> draws = MakeDraws();
    GUI::RecordDraw(draws, 0, {1e3}, std::expected<double, std::string>(5.0), MeasureNumber);
    GUI::RecordDraw(draws, 1, {1.1e3}, std::expected<double, std::string>(4.5), MeasureNumber);

    REQUIRE(draws.Table.Columns.size() == 1);
    CHECK(draws.Table.Columns.front().Node == 1);
    REQUIRE(draws.Table.Runs.size() == 2);
    CHECK(draws.Table.Runs[1].Values == std::vector<double>{1.1e3});
    CHECK(draws.Table.Runs[1].Measures == std::vector<std::optional<double>>{4.5});
    CHECK_FALSE(draws.Table.Runs[1].IsFailed);
    CHECK(draws.Family.Values == std::vector<double>{0.0, 1.0});
    CHECK(draws.Family.Results == std::vector<double>{5.0, 4.5});
}

TEST_CASE("A run that failed is in the table with its reason, and leaves no trace", "[run_jobs]") {
    GUI::MonteCarloDraws<double> draws = MakeDraws();
    GUI::RecordDraw(draws, 0, {1e3}, std::expected<double, std::string>(5.0), MeasureNumber);
    GUI::RecordDraw(draws,
                    1,
                    {2e3},
                    std::expected<double, std::string>(std::unexpected("No convergence")),
                    MeasureNumber);

    REQUIRE(draws.Table.Runs.size() == 2);
    CHECK(draws.Table.Runs[1].IsFailed);
    CHECK(draws.Table.Runs[1].Error == "No convergence");
    CHECK(draws.Table.Runs[1].Measures.empty());
    CHECK(draws.Family.Results.size() == 1);
}

TEST_CASE("A run past the traces kept leaves only its measurements", "[run_jobs]") {
    GUI::MonteCarloDraws<double> draws = MakeDraws();
    draws.Family.Results.resize(GUI::MAX_KEPT_TRACES + 1);
    GUI::RecordDraw(draws, 200, {1e3}, std::expected<double, std::string>(5.0), MeasureNumber);
    CHECK(draws.Table.Runs.size() == 1);
    CHECK(draws.Family.Results.size() == GUI::MAX_KEPT_TRACES + 1);
}

TEST_CASE("The runs of a Monte Carlo analysis store their traces and then their table, or only report why they failed",
          "[run_jobs]") {
    GUI::ResultStore results;
    std::optional<GUI::StepFamily<double>> stored;
    const auto store = [&stored](GUI::StepFamily<double> family) { stored = std::move(family); };

    const std::optional<std::string> failure =
        GUI::StoreMonteCarlo<double>(std::unexpected(std::string("The nominal run failed")),
                                     GUI::MonteCarloTable{},
                                     results,
                                     store);
    CHECK(failure == "The nominal run failed");
    CHECK_FALSE(stored.has_value());
    CHECK_FALSE(results.GetMonteCarlo().has_value());

    GUI::StepFamily<double> family{.Parameter = GUI::MONTE_CARLO_PARAMETER,
                                   .Unit = "",
                                   .Values = {0.0},
                                   .Results = {5.0}};
    const std::optional<std::string> success =
        GUI::StoreMonteCarlo<double>(std::move(family), GUI::MonteCarloTable{.RequestedRuns = 10}, results, store);
    CHECK_FALSE(success.has_value());
    CHECK(stored.has_value());
    REQUIRE(results.GetMonteCarlo().has_value());
    CHECK(results.GetMonteCarlo()->RequestedRuns == 10);
}

TEST_CASE("RunSteps and RunMonteCarlo execute concurrently across threads on Nodalys and preserve run order",
          "[run_jobs]") {
    using namespace std::chrono_literals;
    const TestSupport::NodalysInUse nodalys;
    REQUIRE(Core::GetBackend().GetCapabilities().SupportsConcurrentRuns);

    const GUI::ParameterStepSettings step_settings{.Enabled = true, .Part = "R1"};
    const std::vector<double> step_values = {1e3, 2e3, 3e3, 4e3, 5e3, 6e3, 7e3, 8e3};
    Core::RunControl step_control;
    std::mutex step_mutex;
    std::set<std::thread::id> step_threads;
    std::atomic<int> active_steps = 0;
    std::atomic<int> max_active_steps = 0;

    const GUI::SteppedRuns<double> stepped = GUI::RunSteps<double>(
        step_settings,
        "Ohm",
        step_values,
        step_control,
        [&](const Core::ParameterValue &parameter) -> Core::SimulationRun<double> {
            {
                const std::lock_guard lock(step_mutex);
                step_threads.insert(std::this_thread::get_id());
            }
            const int current = active_steps.fetch_add(1) + 1;
            int previous = max_active_steps.load();
            while (current > previous && !max_active_steps.compare_exchange_weak(previous, current)) {
            }
            std::this_thread::sleep_for(5ms);
            active_steps.fetch_sub(1);
            return {parameter.Value, {{std::format("ran {}", parameter.Value), false}}};
        });

    REQUIRE(stepped.Family.has_value());
    CHECK(stepped.Family->Results == step_values);
    CHECK(step_threads.size() > 1);
    CHECK(max_active_steps.load() > 1);

    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    resistor.SetTolerance(10.0);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(resistor, {1, 0});
    circuit.Add(ground, {0});

    const Core::MonteCarloSettings mc_settings{
        .Enabled = true,
        .Runs = 16,
        .Distribution = Core::ToleranceDistribution::UNIFORM,
        .Seed = 42,
    };

    Core::RunControl parallel_control;
    std::mutex mc_mutex;
    std::set<std::thread::id> mc_threads;
    const auto simulate_resistor = [&](const Core::Circuit &varied, const bool record_thread) {
        if (record_thread) {
            const std::lock_guard lock(mc_mutex);
            mc_threads.insert(std::this_thread::get_id());
            std::this_thread::sleep_for(2ms);
        }
        const double value = varied.GetEntries()[1].Part->GetValue();
        return Core::SimulationRun<double>{value, {{std::format("R1={:.3f}", value), false}}};
    };

    const GUI::MonteCarloRuns<double> parallel_mc = GUI::RunMonteCarlo<double>(
        mc_settings,
        circuit,
        parallel_control,
        [&](const Core::Circuit &varied) { return simulate_resistor(varied, true); },
        MeasureNumber);

    CHECK(mc_threads.size() > 1);
    REQUIRE(parallel_mc.Family.has_value());
    REQUIRE(parallel_mc.Table.Runs.size() == 17);

    // Compare against single-threaded default backend (ngspice) to verify identical order of traces, table, and log
    Core::SetBackend(nullptr);
    REQUIRE_FALSE(Core::GetBackend().GetCapabilities().SupportsConcurrentRuns);
    Core::RunControl serial_control;
    const GUI::MonteCarloRuns<double> serial_mc = GUI::RunMonteCarlo<double>(
        mc_settings,
        circuit,
        serial_control,
        [&](const Core::Circuit &varied) { return simulate_resistor(varied, false); },
        MeasureNumber);

    REQUIRE(serial_mc.Family.has_value());
    CHECK(parallel_mc.Family->Values == serial_mc.Family->Values);
    CHECK(parallel_mc.Family->Results == serial_mc.Family->Results);
    REQUIRE(parallel_mc.Table.Runs.size() == serial_mc.Table.Runs.size());
    for (std::size_t index = 0; index < parallel_mc.Table.Runs.size(); ++index) {
        CHECK(parallel_mc.Table.Runs[index].Values == serial_mc.Table.Runs[index].Values);
        CHECK(parallel_mc.Table.Runs[index].Measures == serial_mc.Table.Runs[index].Measures);
    }
    REQUIRE(parallel_mc.Messages.size() == serial_mc.Messages.size());
    for (std::size_t index = 0; index < parallel_mc.Messages.size(); ++index) {
        CHECK(parallel_mc.Messages[index].Text == serial_mc.Messages[index].Text);
    }
}
