/**
 * @file    cursor_widgets_tests.cpp
 * @brief   Tests for what the measurement cursors of the Output window give the statistics.
 */

#include "windows/output_tabs/cursor_widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>

TEST_CASE("The statistics read the traces at the cursors only while they are shown", "[cursor_widgets]") {
    GUI::PlotCursors cursors{.Shown = true, .Placed = true, .A = 1.0, .B = 2.0};
    const std::optional<GUI::PlotCursors> shown = GUI::GetShownCursors(cursors);
    REQUIRE(shown);
    CHECK(shown->A == 1.0);
    CHECK(shown->B == 2.0);
    cursors.Shown = false;
    CHECK_FALSE(GUI::GetShownCursors(cursors));
}

TEST_CASE("The statistics measure between the cursors while they are shown, otherwise what the plot shows",
          "[cursor_widgets]") {
    GUI::PlotCursors cursors{.Shown = true, .Placed = true, .A = 3.0, .B = 1.0};
    const GUI::PlotSpan view{.From = 0.0, .To = 10.0};
    CHECK(GUI::MeasuredSpan(cursors, view).From == 3.0);
    CHECK(GUI::MeasuredSpan(cursors, view).To == 1.0);
    cursors.Shown = false;
    CHECK(GUI::MeasuredSpan(cursors, view).To == 10.0);
}

TEST_CASE("A measured span is kept within the run, earlier end first", "[cursor_widgets]") {
    const GUI::PlotSpan run{.From = 0.0, .To = 10.0};
    const GUI::PlotSpan reversed = GUI::ClipSpanToRun({.From = 12.0, .To = 4.0}, run);
    CHECK(reversed.From == 4.0);
    CHECK(reversed.To == 10.0);
    // A span outside the run measures the whole run
    const GUI::PlotSpan outside = GUI::ClipSpanToRun({.From = 20.0, .To = 30.0}, run);
    CHECK(outside.From == 0.0);
    CHECK(outside.To == 10.0);
}
