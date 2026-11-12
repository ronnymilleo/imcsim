/**
 * @file    parameter_step_tests.cpp
 * @brief   Tests for the labels and joined curves of a stepped run.
 */

#include "results/parameter_step.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("Step values read as the parameter, its value and its unit", "[parameter_step]") {
    CHECK(GUI::FormatStepValue("R1", 2.2e3, "Ohm") == "R1=2.2kOhm");
    CHECK(GUI::FormatStepValue("T", 85.0, "°C") == "T=85°C");
}

TEST_CASE("The runs of a Monte Carlo analysis are labeled Nominal and Run n", "[parameter_step]") {
    CHECK(GUI::FormatStepValue(GUI::MONTE_CARLO_PARAMETER, 0.0, "") == "Nominal");
    CHECK(GUI::FormatStepValue(GUI::MONTE_CARLO_PARAMETER, 17.0, "") == "Run 17");
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

TEST_CASE("The steps of an operating point read as a sweep of the parameter", "[parameter_step]") {
    const GUI::StepFamily<Core::OperatingPoint> family{
        .Parameter = "R2",
        .Unit = "Ohm",
        .Values = {1e3, 2e3},
        .Results = {{.NodeVoltages = {0.0, 10.0, 5.0}, .Currents = {{.Name = "R1", .Current = 5e-3}}, .Warnings = {}},
                    {.NodeVoltages = {0.0, 10.0, 6.67},
                     .Currents = {{.Name = "R1", .Current = 3.33e-3}},
                     .Warnings = {}}},
        .Selected = 0};
    const Core::DCSweep sweep = GUI::JoinOperatingPointSteps(family);
    CHECK(sweep.SweptSource == "R2");
    CHECK(sweep.SweptUnit == "Ohm");
    CHECK(sweep.SweptValues == std::vector<double>{1e3, 2e3});
    CHECK(sweep.SteppedSource.empty());
    REQUIRE(sweep.Curves.size() == 1);
    // Node 2 holds one voltage per step, and R1 one current per step
    CHECK(sweep.Curves.front().NodeVoltages[2] == std::vector<double>{5.0, 6.67});
    REQUIRE(sweep.Curves.front().Currents.size() == 1);
    CHECK(sweep.Curves.front().Currents.front().Values == std::vector<double>{5e-3, 3.33e-3});
}
