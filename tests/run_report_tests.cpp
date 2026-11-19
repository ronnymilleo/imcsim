/**
 * @file    run_report_tests.cpp
 * @brief   Tests for what the last run of each analysis reported besides its result, and how it went.
 */

#include "analysis/run_report.h"

#include "results/result_store.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

GUI::RunStatus MakeStatus(const bool is_from_error_stream) {
    return {.Error = std::nullopt, .Messages = {{"Circuit: rc", is_from_error_stream}}, .Seconds = 0.5};
}

} // namespace

TEST_CASE("Ngspice warned when any of its messages came from its error stream", "[run_report]") {
    CHECK_FALSE(GUI::RunStatus{}.HasNgspiceWarnings());
    CHECK_FALSE(MakeStatus(false).HasNgspiceWarnings());
    CHECK(MakeStatus(true).HasNgspiceWarnings());
}

TEST_CASE("A recorded run replaces the last one of its analysis and becomes the last run", "[run_report]") {
    GUI::RunReport report;
    CHECK_FALSE(report.GetLastRun().has_value());
    CHECK(report.GetFinishedRunCount() == 0);

    report.Record(GUI::Analysis::TRANSIENT, MakeStatus(false));
    report.Record(GUI::Analysis::AC_SWEEP, {.Error = "No AC source", .Messages = {}, .Seconds = std::nullopt});
    CHECK(report.GetLastRun() == GUI::Analysis::AC_SWEEP);
    CHECK(report.GetFinishedRunCount() == 2);
    CHECK(report.GetStatus(GUI::Analysis::TRANSIENT).Seconds == 0.5);
    CHECK(report.GetStatus(GUI::Analysis::AC_SWEEP).Error == "No AC source");

    // Clearing forgets the status, but the count and the last run stay
    report.Clear(GUI::Analysis::AC_SWEEP);
    CHECK_FALSE(report.GetStatus(GUI::Analysis::AC_SWEEP).Error.has_value());
    CHECK(report.GetLastRun() == GUI::Analysis::AC_SWEEP);
    CHECK(report.GetFinishedRunCount() == 2);
}

TEST_CASE("A run failed while it has an error, and has no outcome once its result is gone", "[run_report]") {
    GUI::RunReport report;
    GUI::ResultStore results;
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, {}) == GUI::RunOutcome::NONE);

    report.Record(GUI::Analysis::TRANSIENT, {.Error = "Timestep too small", .Messages = {}, .Seconds = std::nullopt});
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, {}) == GUI::RunOutcome::FAILED);

    report.Record(GUI::Analysis::TRANSIENT, MakeStatus(false));
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, {}) == GUI::RunOutcome::NONE);
    results.SetTransient(Core::Transient{});
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, {}) == GUI::RunOutcome::SUCCEEDED);
}

TEST_CASE("A run succeeded with warnings when its result or ngspice warned, and a short transient stopped",
          "[run_report]") {
    GUI::RunReport report;
    GUI::ResultStore results;
    results.SetTransient(Core::Transient{});
    report.Record(GUI::Analysis::TRANSIENT, MakeStatus(false));
    const std::vector<std::string> warnings = {"Q1 past its maximum voltage"};
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, warnings) == GUI::RunOutcome::SUCCEEDED_WITH_WARNINGS);

    report.Record(GUI::Analysis::TRANSIENT, MakeStatus(true));
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, {}) == GUI::RunOutcome::SUCCEEDED_WITH_WARNINGS);

    Core::Transient stopped;
    stopped.IsComplete = false;
    results.SetTransient(stopped);
    CHECK(report.GetOutcome(GUI::Analysis::TRANSIENT, results, warnings) == GUI::RunOutcome::STOPPED);
}
