/**
 * @file    parameter_step_tests.cpp
 * @brief   Tests for the values and labels of a stepped parameter.
 */

#include "parameter_step.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <vector>

using Catch::Matchers::WithinRel;

TEST_CASE("A stepped range includes both ends and may go downward", "[parameter_step]") {
    GUI::ParameterStepSettings settings{.Enabled = true, .Start = 0.0, .Stop = 100.0, .Step = 25.0};
    const auto values = GUI::ListStepValues(settings, "°C");
    REQUIRE(values);
    CHECK(*values == std::vector<double>{0.0, 25.0, 50.0, 75.0, 100.0});

    settings.Start = 1e3;
    settings.Stop = 0.4e3;
    settings.Step = -0.2e3;
    const auto downward = GUI::ListStepValues(settings, "Ohm");
    REQUIRE(downward);
    REQUIRE(downward->size() == 4);
    CHECK_THAT(downward->back(), WithinRel(400.0, 1e-9));
}

TEST_CASE("A stepped range rejects a step the wrong way and too many runs", "[parameter_step]") {
    GUI::ParameterStepSettings settings{.Enabled = true, .Start = 0.0, .Stop = 100.0, .Step = -1.0};
    CHECK_FALSE(GUI::ListStepValues(settings, "°C"));
    settings.Step = 0.0;
    CHECK_FALSE(GUI::ListStepValues(settings, "°C"));
    settings.Step = 1.0;
    CHECK_FALSE(GUI::ListStepValues(settings, "°C"));
    settings.Step = 100.0 / (GUI::MAX_PARAMETER_STEPS - 1);
    CHECK(GUI::ListStepValues(settings, "°C"));
}

TEST_CASE("A stepped list reads values with suffixes and any separator", "[parameter_step]") {
    GUI::ParameterStepSettings settings{.Enabled = true, .UsesList = true, .List = "1k, 2.2k;4k7  10kOhm"};
    const auto values = GUI::ListStepValues(settings, "Ohm");
    REQUIRE(values);
    REQUIRE(values->size() == 4);
    CHECK_THAT((*values)[1], WithinRel(2.2e3, 1e-9));
    CHECK_THAT((*values)[2], WithinRel(4.7e3, 1e-9));
    CHECK_THAT((*values)[3], WithinRel(10e3, 1e-9));

    settings.List = "1k, x";
    const auto invalid = GUI::ListStepValues(settings, "Ohm");
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().contains("\"x\""));
    settings.List = " , ";
    CHECK_FALSE(GUI::ListStepValues(settings, "Ohm"));
}

TEST_CASE("Step labels name the part, or T for the temperature", "[parameter_step]") {
    CHECK(GUI::GetParameterLabel({.Part = "R1"}) == "R1");
    CHECK(GUI::GetParameterLabel({}) == "T");
    CHECK(GUI::FormatStepValue("R1", 2.2e3, "Ohm") == "R1=2.2kOhm");
    CHECK(GUI::FormatStepValue("T", 85.0, "°C") == "T=85°C");
}

TEST_CASE("Stepped DC sweeps join into one family of curves", "[parameter_step]") {
    GUI::StepFamily<Core::DCSweep> family{.Parameter = "R1", .Unit = "Ohm", .Values = {1e3, 2e3}, .Results = {}};
    for (const double level : {1.0, 2.0}) {
        Core::DCSweep sweep{.SweptSource = "V1", .SweptUnit = "V", .SweptValues = {0.0, 1.0}};
        sweep.Curves.push_back({.StepValue = 0.0, .NodeVoltages = {{0.0, 0.0}, {0.0, level}}, .Currents = {}});
        family.Results.push_back(std::move(sweep));
    }
    const Core::DCSweep joined = GUI::JoinDCSweepSteps(std::move(family));
    CHECK(joined.SweptSource == "V1");
    CHECK(joined.SteppedSource == "R1");
    CHECK(joined.SteppedUnit == "Ohm");
    REQUIRE(joined.Curves.size() == 2);
    CHECK(joined.Curves[1].StepValue == 2e3);
    CHECK(joined.Curves[1].NodeVoltages[1][1] == 2.0);
}
