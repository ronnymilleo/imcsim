/**
 * @file    live_run_plan_tests.cpp
 * @brief   Tests for how a live transient runs: its pace, the stop time that caps it, and the warning at that cap.
 */

#include "analysis/live_run_plan.h"

#include "check_fixtures.h"
#include "schematic/simulation_settings.h"
#include "simulation/check_transient.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <expected>
#include <string>
#include <utility>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

/// The tolerance of a stop time worked out from a product of settings.
constexpr double RELATIVE_TOLERANCE = 1e-12;

Core::TransientRun MakeRun(const bool is_complete) {
    Core::Transient transient;
    transient.IsComplete = is_complete;
    return {.Result = std::move(transient), .Messages = {}};
}

} // namespace

TEST_CASE("A live run covers its stop time in the live seconds and stops after an hour on the clock",
          "[live_run_plan]") {
    const SourceAndResistor setup;
    const GUI::LiveRunPlan plan = GUI::PlanLiveRun({.StopTime = 10e-3, .TimeStep = 10e-6}, setup.Circuit, 10.0);
    CHECK_THAT(plan.Pace, WithinRel(1e-3, RELATIVE_TOLERANCE));
    CHECK_THAT(plan.StopTime, WithinRel(1e-3 * GUI::LIVE_RUN_LIMIT_SECONDS, RELATIVE_TOLERANCE));
}

TEST_CASE("A live run with a fine step stops one step short of the most points a transient may span",
          "[live_run_plan]") {
    const SourceAndResistor setup;
    const GUI::LiveRunPlan plan = GUI::PlanLiveRun({.StopTime = 10e-3, .TimeStep = 1e-6}, setup.Circuit, 10.0);
    CHECK_THAT(plan.StopTime, WithinRel(1e-6 * (Core::MAX_TRANSIENT_POINTS - 1), RELATIVE_TOLERANCE));
}

TEST_CASE("A noise source that steps finer than the time step caps a live run sooner", "[live_run_plan]") {
    SourceAndResistor setup;
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::NOISE);
    setup.Source.SetNoise({.WhiteAmplitude = 1e-3, .TimeStep = 0.5e-6});
    const GUI::LiveRunPlan plan = GUI::PlanLiveRun({.StopTime = 10e-3, .TimeStep = 10e-6}, setup.Circuit, 10.0);
    CHECK_THAT(plan.StopTime, WithinRel(0.5e-6 * (Core::MAX_TRANSIENT_POINTS - 1), RELATIVE_TOLERANCE));
    // The pace still comes from the settings, since the live seconds cover their stop time
    CHECK_THAT(plan.Pace, WithinRel(1e-3, RELATIVE_TOLERANCE));
}

TEST_CASE("Only a live run that completed, and so reached its limit, gets the limit warning", "[live_run_plan]") {
    Core::TransientRun stopped = MakeRun(false);
    GUI::AddLiveLimitWarning(stopped);
    REQUIRE(stopped.Result.has_value());
    CHECK(stopped.Result->Warnings.empty());

    Core::TransientRun failed{.Result = std::unexpected(std::string("No convergence")), .Messages = {}};
    GUI::AddLiveLimitWarning(failed);
    CHECK_FALSE(failed.Result.has_value());

    Core::TransientRun complete = MakeRun(true);
    GUI::AddLiveLimitWarning(complete);
    REQUIRE(complete.Result.has_value());
    REQUIRE(complete.Result->Warnings.size() == 1);
    CHECK_THAT(complete.Result->Warnings.front(), ContainsSubstring("reached its limit"));
}
