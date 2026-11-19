/**
 * @file    trace_fit_state_tests.cpp
 * @brief   Tests for when the axes of a plot fit, and for the math traces they fit to.
 */

#include "plots/math_channels.h"
#include "plots/trace_fit_state.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Every axis fits once for each new result", "[trace_fit]") {
    GUI::TraceFitState fit;
    CHECK(fit.TakeNewResult(1));
    CHECK_FALSE(fit.TakeNewResult(1));
    CHECK(fit.TakeNewResult(2));
    CHECK_FALSE(fit.TakeNewResult(2));
}

TEST_CASE("An axis fits when its measured traces change, but not when the last one is removed", "[trace_fit]") {
    GUI::TraceFitState fit;
    CHECK(fit.TakeNodesChanged({1}));
    CHECK_FALSE(fit.TakeNodesChanged({1}));
    CHECK(fit.TakeNodesChanged({1, 3}));
    CHECK_FALSE(fit.TakeNodesChanged({}));
    // Back from nothing, the trace counts as new
    CHECK(fit.TakeNodesChanged({1}));
}

TEST_CASE("Voltages, currents and math channels are tracked apart", "[trace_fit]") {
    GUI::TraceFitState fit;
    CHECK(fit.TakeCurrentsChanged({"R1"}));
    CHECK(fit.TakeMathChanged({"M1: V(1)-V(2)V"}));
    CHECK_FALSE(fit.TakeCurrentsChanged({"R1"}));
    CHECK_FALSE(fit.TakeMathChanged({"M1: V(1)-V(2)V"}));
    CHECK(fit.TakeNodesChanged({2}));
    CHECK_FALSE(fit.TakeCurrentsChanged({"R1"}));
}

TEST_CASE("A frame tells at once which axes fit, and the next frame with the same traces fits none", "[trace_fit]") {
    GUI::TraceFitState fit;
    const GUI::TraceFits first = fit.TakeFits(1, {.Nodes = {1}, .Currents = {"R1"}, .MathLabels = {"M1V"}});
    CHECK(first
          == GUI::TraceFits{.IsNewResult = true,
                            .HaveVoltagesChanged = true,
                            .HaveCurrentsChanged = true,
                            .HasMathChanged = true});
    CHECK(fit.TakeFits(1, {.Nodes = {1}, .Currents = {"R1"}, .MathLabels = {"M1V"}}) == GUI::TraceFits{});
    // Only the currents changed, so only their axis fits
    CHECK(fit.TakeFits(1, {.Nodes = {1}, .Currents = {"R1", "C1"}, .MathLabels = {"M1V"}})
          == GUI::TraceFits{.IsNewResult = false,
                            .HaveVoltagesChanged = false,
                            .HaveCurrentsChanged = true,
                            .HasMathChanged = false});
}

TEST_CASE("Something besides the result can ask every axis to fit, and the traces are still kept", "[trace_fit]") {
    GUI::TraceFitState fit;
    CHECK(fit.TakeFits(1, {.Nodes = {1}, .Currents = {}, .MathLabels = {}}).IsNewResult);
    const GUI::TraceFits forced = fit.TakeFits(1, {.Nodes = {1}, .Currents = {}, .MathLabels = {}}, true);
    CHECK(forced.IsNewResult);
    CHECK_FALSE(forced.HaveVoltagesChanged);
    // The version was taken even though the fit was forced, so the same version is not new afterwards
    CHECK_FALSE(fit.TakeFits(1, {.Nodes = {1}, .Currents = {}, .MathLabels = {}}).IsNewResult);
}

TEST_CASE("Traces kept outside a fit state follow the same rule", "[trace_fit]") {
    std::vector<std::string> shown;
    CHECK(GUI::TakeTracesChanged(shown, {"R1", "C1"}));
    CHECK(shown == std::vector<std::string>{"R1", "C1"});
    CHECK_FALSE(GUI::TakeTracesChanged(shown, {"R1", "C1"}));
    CHECK_FALSE(GUI::TakeTracesChanged(shown, {}));
    CHECK(shown.empty());
}

TEST_CASE("The third axis takes the unit its math traces share", "[math_channels]") {
    const GUI::MathTrace volts{.Label = "M1", .Values = {}, .Unit = "V", .Axis = ImAxis_Y1, .Color = 0};
    const GUI::MathTrace watts{.Label = "M2", .Values = {}, .Unit = "W", .Axis = ImAxis_Y3, .Color = 0};
    const GUI::MathTrace ohms{.Label = "M3", .Values = {}, .Unit = "Ohm", .Axis = ImAxis_Y3, .Color = 0};

    CHECK(GUI::ThirdAxisUnit({volts}).empty());
    CHECK(GUI::ThirdAxisUnit({volts, watts}) == "W");
    CHECK(GUI::ThirdAxisUnit({watts, watts}) == "W");
    CHECK(GUI::ThirdAxisUnit({watts, ohms}).empty());

    CHECK(GUI::UsesAxis({volts, watts}, ImAxis_Y3));
    CHECK_FALSE(GUI::UsesAxis({volts}, ImAxis_Y2));
    CHECK(GUI::ListMathLabels({volts, watts}) == std::vector<std::string>{"M1V", "M2W"});
}
