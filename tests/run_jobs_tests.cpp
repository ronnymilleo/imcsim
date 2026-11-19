/**
 * @file    run_jobs_tests.cpp
 * @brief   Tests for the building blocks that put an analysis on the worker thread: what the runs of a Monte Carlo
 *          analysis keep, and how their results are stored.
 */

#include "analysis/run_jobs.h"

#include "results/monte_carlo_results.h"
#include "results/parameter_step.h"
#include "results/result_store.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace {

/// A run of these tests makes a number, which it measures as the mean of node 1.
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
