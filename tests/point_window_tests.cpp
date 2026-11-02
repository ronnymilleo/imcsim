/**
 * @file    point_window_tests.cpp
 * @brief   Tests for the stretch of a long transient that the plot shows.
 */

#include "point_window.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace {

// A transient of the given number of points, with time n at index n, node 1 at twice the time and one current
Core::Transient CountingTransient(const std::size_t point_count) {
    Core::Transient transient;
    for (std::size_t index = 0; index < point_count; ++index) {
        transient.Times.push_back(static_cast<double>(index));
    }
    transient.NodeVoltages = {std::vector<double>(point_count, 0.0), {}};
    for (const double time : transient.Times) {
        transient.NodeVoltages[1].push_back(2.0 * time);
    }
    transient.Currents = {{"R1", transient.Times}};
    transient.Warnings = {"warned"};
    transient.IsComplete = false;
    return transient;
}

} // namespace

TEST_CASE("A result that fits the plot is shown whole, and a long one by a window of the size chosen",
          "[point_window]") {
    const GUI::PointWindow whole = GUI::ClampWindow(500, 100, 2000);
    CHECK(whole.First == 0);
    CHECK(whole.Count == 500);

    const GUI::PointWindow inside = GUI::ClampWindow(1'000'000, 12'345, 2000);
    CHECK(inside.First == 12'345);
    CHECK(inside.Count == 2000);

    // The user can ask for a window from the smallest to the largest size
    CHECK(GUI::ClampWindow(1'000'000, 0, GUI::MIN_WINDOW_POINTS).Count == 1000);
    CHECK(GUI::ClampWindow(1'000'000, 0, GUI::MAX_WINDOW_POINTS).Count == 10'000);
    CHECK(GUI::ClampWindow(0, 0, 2000).Count == 0);
}

TEST_CASE("A window never runs past the end of the result", "[point_window]") {
    const GUI::PointWindow late = GUI::ClampWindow(10'000, 9'999, 2000);
    CHECK(late.First == 8000);
    CHECK(late.Count == 2000);

    const GUI::PointWindow end = GUI::GetEndWindow(10'000, 2500);
    CHECK(end.First == 7500);
    CHECK(end.Count == 2500);
    CHECK(GUI::GetEndWindow(30, 2000).First == 0);
}

TEST_CASE("A slice keeps the same points of every time, node and current, and the flags of the result",
          "[point_window]") {
    const Core::Transient slice = GUI::SliceTransient(CountingTransient(100), {.First = 10, .Count = 3});
    CHECK(slice.Times == std::vector<double>{10.0, 11.0, 12.0});
    REQUIRE(slice.NodeVoltages.size() == 2);
    CHECK(slice.NodeVoltages[0] == std::vector<double>{0.0, 0.0, 0.0});
    CHECK(slice.NodeVoltages[1] == std::vector<double>{20.0, 22.0, 24.0});
    REQUIRE(slice.Currents.size() == 1);
    CHECK(slice.Currents[0].Name == "R1");
    CHECK(slice.Currents[0].Values == std::vector<double>{10.0, 11.0, 12.0});
    CHECK(slice.Warnings == std::vector<std::string>{"warned"});
    CHECK_FALSE(slice.IsComplete);
}

TEST_CASE("A slice by time reaches one point past each edge, so the lines cross the edges of the plot",
          "[point_window]") {
    const Core::Transient transient = CountingTransient(100);
    const Core::Transient slice = GUI::SliceTransientByTime(transient, 10.5, 12.5);
    CHECK(slice.Times == std::vector<double>{10.0, 11.0, 12.0, 13.0});

    // At the ends of the run there is nothing past the edge to add
    CHECK(GUI::SliceTransientByTime(transient, -5.0, 1.0).Times == std::vector<double>{0.0, 1.0, 2.0});
    CHECK(GUI::SliceTransientByTime(transient, 98.0, 200.0).Times == std::vector<double>{97.0, 98.0, 99.0});
    CHECK(GUI::SliceTransientByTime(Core::Transient{}, 0.0, 1.0).Times.empty());
}

TEST_CASE("Math traces worked out over a whole run are cut to the window, with what they say about themselves",
          "[point_window]") {
    GUI::MathTrace trace;
    trace.Label = "M1: V(1)-V(2)";
    trace.Unit = "V";
    trace.Axis = ImAxis_Y2;
    trace.Color = 0xFF112233;
    for (int index = 0; index < 100; ++index) {
        trace.Values.push_back(static_cast<double>(index));
    }

    const std::vector<GUI::MathTrace> slices = GUI::SliceMathTraces({trace}, {.First = 10, .Count = 3});
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].Values == std::vector<double>{10.0, 11.0, 12.0});
    CHECK(slices[0].Label == trace.Label);
    CHECK(slices[0].Unit == "V");
    CHECK(slices[0].Axis == ImAxis_Y2);
    CHECK(slices[0].Color == trace.Color);

    // A trace worked out over a shorter run has no values past its end
    CHECK(GUI::SliceMathTraces({trace}, {.First = 98, .Count = 5})[0].Values == std::vector<double>{98.0, 99.0});
    CHECK(GUI::SliceMathTraces({trace}, {.First = 200, .Count = 5})[0].Values.empty());
}
