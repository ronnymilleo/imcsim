/**
 * @file    transient_tab_tests.cpp
 * @brief   Tests for what the analog plot of the Transient tab shows beside its logic lanes.
 */

#include "windows/output_tabs/transient_tab.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace {

GUI::MathTrace MakeMath(const ImAxis axis) {
    return {.Label = "M1", .Values = {}, .Unit = "V", .Axis = axis, .Color = 0};
}

} // namespace

TEST_CASE("The analog plot shows its measured voltages and currents, logic lanes left out", "[transient_tab]") {
    const GUI::ShownTraces shown{.Nodes = {1}, .Currents = {"R1"}, .MathLabels = {}};
    const GUI::AnalogPlan plan = GUI::PlanAnalog(shown, {}, {2, 3}, true);
    CHECK(plan
          == GUI::AnalogPlan{.LaneNodes = {2, 3},
                             .ShowsCurrents = true,
                             .HasVoltages = true,
                             .HasLanes = true,
                             .IsShown = true});
}

TEST_CASE("A circuit measured only at logic nodes shows its lanes without an empty analog plot", "[transient_tab]") {
    const GUI::ShownTraces shown{.Nodes = {}, .Currents = {}, .MathLabels = {}};
    CHECK_FALSE(GUI::PlanAnalog(shown, {}, {2}, true).IsShown);
    // Without lanes the analog plot stays, even empty, so the tab is never blank
    CHECK(GUI::PlanAnalog(shown, {}, {}, false).IsShown);
}

TEST_CASE("A math channel keeps the analog plot, and on the voltage axis counts as a voltage", "[transient_tab]") {
    const GUI::ShownTraces shown{.Nodes = {}, .Currents = {}, .MathLabels = {"M1V"}};
    const GUI::AnalogPlan on_voltage_axis = GUI::PlanAnalog(shown, {MakeMath(ImAxis_Y1)}, {2}, true);
    CHECK(on_voltage_axis.IsShown);
    CHECK(on_voltage_axis.HasVoltages);
    const GUI::AnalogPlan on_third_axis = GUI::PlanAnalog(shown, {MakeMath(ImAxis_Y3)}, {2}, true);
    CHECK(on_third_axis.IsShown);
    CHECK_FALSE(on_third_axis.HasVoltages);
}

TEST_CASE("A run too short to cut into windows has no window size or scroll bar", "[transient_tab]") {
    const auto short_run = static_cast<std::size_t>(GUI::MIN_WINDOW_POINTS);
    CHECK(GUI::GetWindowAvailability(short_run, false, short_run) == GUI::WindowAvailability{});
    // A longer run picks the size of its window, and scrolls once the window is shorter than the run
    CHECK(GUI::GetWindowAvailability(short_run * 4, false, short_run * 4)
          == GUI::WindowAvailability{.HasScrollbar = false, .HasSize = true});
    CHECK(GUI::GetWindowAvailability(short_run * 4, false, short_run)
          == GUI::WindowAvailability{.HasScrollbar = true, .HasSize = true});
}

TEST_CASE("A live run has no window size, and scrolls only while it is longer than its window", "[transient_tab]") {
    CHECK(GUI::GetWindowAvailability(100, true, 100) == GUI::WindowAvailability{});
    CHECK(GUI::GetWindowAvailability(300, true, 100)
          == GUI::WindowAvailability{.HasScrollbar = true, .HasSize = false});
}
