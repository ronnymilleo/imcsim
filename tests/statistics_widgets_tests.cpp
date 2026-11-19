/**
 * @file    statistics_widgets_tests.cpp
 * @brief   Tests for the readings of the cursors that the statistics panels of the Output window list.
 */

#include "plots/statistics_table.h"
#include "windows/output_tabs/statistics_widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace {

const GUI::PlotCursors SHOWN_CURSORS{.Shown = true, .Placed = true, .A = 1e-3, .B = 3e-3};

} // namespace

TEST_CASE("The cursors read where each one is, then how far apart they are", "[statistics_widgets]") {
    const GUI::CursorReadings readings = GUI::ReadCursors(SHOWN_CURSORS, "s", {{"Gap", "2ms"}});
    CHECK(readings
          == GUI::CursorReadings{{"A", GUI::FormatQuantity(1e-3, "s")},
                                 {"B", GUI::FormatQuantity(3e-3, "s")},
                                 {"Gap", "2ms"}});
}

TEST_CASE("Hidden cursors read nothing, so their table is left out", "[statistics_widgets]") {
    GUI::PlotCursors hidden = SHOWN_CURSORS;
    hidden.Shown = false;
    CHECK(GUI::ReadCursors(hidden, "s", {GUI::ReadCursorDifference(hidden, "s")}).empty());
}

TEST_CASE("The distance between the cursors reads as a difference, a frequency or decades", "[statistics_widgets]") {
    CHECK(GUI::ReadCursorDifference(SHOWN_CURSORS, "s") == GUI::CursorReading{"B-A", GUI::FormatQuantity(2e-3, "s")});
    CHECK(GUI::ReadCursorFrequency(SHOWN_CURSORS) == GUI::CursorReading{"1/(B-A)", GUI::FormatQuantity(500.0, "Hz")});
    const GUI::PlotCursors decade{.Shown = true, .Placed = true, .A = 10.0, .B = 1000.0};
    CHECK(GUI::ReadCursorDecades(decade) == GUI::CursorReading{"B/A", "2.00 dec"});
}

TEST_CASE("A distance with no meaning reads as no value", "[statistics_widgets]") {
    // Both cursors at the same time span no period, and a frequency at or below zero has no place on a decade axis
    const GUI::PlotCursors together{.Shown = true, .Placed = true, .A = 2.0, .B = 2.0};
    CHECK(GUI::ReadCursorFrequency(together).second == GUI::NO_VALUE);
    const GUI::PlotCursors at_zero{.Shown = true, .Placed = true, .A = 0.0, .B = 100.0};
    CHECK(GUI::ReadCursorDecades(at_zero).second == GUI::NO_VALUE);
}
