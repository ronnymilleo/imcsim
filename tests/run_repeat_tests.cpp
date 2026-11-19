/**
 * @file    run_repeat_tests.cpp
 * @brief   Tests for how a run of an analysis repeats: once, once per value of a stepped parameter, or once per Monte
 *          Carlo draw.
 */

#include "analysis/run_repeat.h"

#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <variant>
#include <vector>

namespace {

/// A 5 V source across a 1k resistor, which a test gives a tolerance when Monte Carlo needs one.
struct Divider {
    Core::VoltageSource Supply;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Divider() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        Load.SetName("R1");
        Load.SetValue(1e3);
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Load, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

/// R1 from 1k to 3k in steps of 1k, three runs.
const GUI::ParameterStepSettings RESISTANCE_STEP =
    {.Enabled = true, .Part = "R1", .UsesList = false, .Start = 1e3, .Stop = 3e3, .Step = 1e3, .List = ""};

GUI::SimulationSettings MakeSettings(const GUI::Analysis analysis) {
    GUI::SimulationSettings settings;
    settings.Selected = analysis;
    return settings;
}

} // namespace

TEST_CASE("An analysis runs once when neither the step nor Monte Carlo is on", "[run_repeat]") {
    const Divider divider;
    const auto repeat = GUI::PlanRunRepeat(MakeSettings(GUI::Analysis::TRANSIENT), divider.Circuit);
    REQUIRE(repeat.has_value());
    CHECK(GUI::IsSingleRun(*repeat));
}

TEST_CASE("An analysis that never repeats runs once whatever the step and Monte Carlo say", "[run_repeat]") {
    const Divider divider;
    GUI::SimulationSettings settings = MakeSettings(GUI::Analysis::NOISE);
    settings.ParameterStep = RESISTANCE_STEP;
    settings.MonteCarlo.Enabled = true;
    const auto repeat = GUI::PlanRunRepeat(settings, divider.Circuit);
    REQUIRE(repeat.has_value());
    CHECK(GUI::IsSingleRun(*repeat));
}

TEST_CASE("A stepped analysis gets the values of the step and the unit of the part", "[run_repeat]") {
    const Divider divider;
    GUI::SimulationSettings settings = MakeSettings(GUI::Analysis::AC_SWEEP);
    settings.ParameterStep = RESISTANCE_STEP;
    const auto repeat = GUI::PlanRunRepeat(settings, divider.Circuit);
    REQUIRE(repeat.has_value());
    const auto *plan = std::get_if<GUI::StepPlan>(&*repeat);
    REQUIRE(plan != nullptr);
    CHECK(plan->Values == std::vector<double>{1e3, 2e3, 3e3});
    CHECK(plan->Unit == "Ohm");
    CHECK(plan->Settings == RESISTANCE_STEP);
}

TEST_CASE("A step without valid values says why instead of running", "[run_repeat]") {
    const Divider divider;
    GUI::SimulationSettings settings = MakeSettings(GUI::Analysis::TRANSIENT);
    settings.ParameterStep = RESISTANCE_STEP;
    settings.ParameterStep.UsesList = true;
    settings.ParameterStep.List = "abc";
    const auto repeat = GUI::PlanRunRepeat(settings, divider.Circuit);
    REQUIRE_FALSE(repeat.has_value());
    CHECK_FALSE(repeat.error().empty());
}

TEST_CASE("Monte Carlo wins over the step, and needs a part with a tolerance", "[run_repeat]") {
    Divider divider;
    GUI::SimulationSettings settings = MakeSettings(GUI::Analysis::DC_SWEEP);
    settings.ParameterStep = RESISTANCE_STEP;
    settings.MonteCarlo = {.Enabled = true, .Runs = 10, .Seed = 1234};

    // No part has a tolerance yet, so there is nothing to draw
    const auto refused = GUI::PlanRunRepeat(settings, divider.Circuit);
    REQUIRE_FALSE(refused.has_value());
    CHECK_FALSE(refused.error().empty());

    divider.Load.SetTolerance(5.0);
    const auto repeat = GUI::PlanRunRepeat(settings, divider.Circuit);
    REQUIRE(repeat.has_value());
    const auto *monte_carlo = std::get_if<Core::MonteCarloSettings>(&*repeat);
    REQUIRE(monte_carlo != nullptr);
    CHECK(*monte_carlo == settings.MonteCarlo);
}
