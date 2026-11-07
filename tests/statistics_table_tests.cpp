/**
 * @file    statistics_table_tests.cpp
 * @brief   Tests for the statistics of the transient traces as one table, with a column per trace.
 */

#include "plots/statistics_table.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

namespace {

/// Points over the five periods of the test waves.
constexpr std::size_t POINT_COUNT = 5001;
/// Frequency of the test waves, 1 kHz as in the RC example.
constexpr double FREQUENCY = 1e3;
constexpr double STOP_TIME = 5e-3;

struct Waves {
    std::vector<double> Times;
    std::vector<double> Input;
    std::vector<double> Output;
};

/**
 * @brief   A 1 V sine and a smaller one, as V(1) and V(2) of the RC example.
 */
Waves MakeWaves() {
    Waves waves;
    for (std::size_t index = 0; index < POINT_COUNT; ++index) {
        const double time = STOP_TIME * static_cast<double>(index) / static_cast<double>(POINT_COUNT - 1);
        const double phase = 2.0 * std::numbers::pi * FREQUENCY * time;
        waves.Times.push_back(time);
        waves.Input.push_back(std::sin(phase));
        waves.Output.push_back(0.8545 * std::sin(phase - 0.56));
    }
    return waves;
}

const GUI::StatisticsRow &FindRow(const std::vector<GUI::StatisticsRow> &rows, const std::string &name) {
    for (const GUI::StatisticsRow &row : rows) {
        if (row.Name == name) {
            return row;
        }
    }
    FAIL("No row " << name);
    return rows.front();
}

} // namespace

TEST_CASE("The statistics read across: a row per quantity, a column per trace", "[statistics_table]") {
    const Waves waves = MakeWaves();
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildTransientStatistics(
        waves.Times, {{.Values = waves.Input, .Unit = "V"}, {.Values = waves.Output, .Unit = "V"}},
        {.From = 0.0, .To = STOP_TIME}, std::nullopt);
    REQUIRE(rows.size() == 7);
    for (const GUI::StatisticsRow &row : rows) {
        CHECK(row.Cells.size() == 2);
    }
    // Four significant digits on every reading, so Max reads 1.000V and not 1000mV
    CHECK(FindRow(rows, "Pk-Pk").Cells[0].Text == "2.000V");
    CHECK(FindRow(rows, "Max").Cells[0].Text == "1.000V");
    CHECK(FindRow(rows, "Max").Cells[1].Text == "854.5mV");
    CHECK(FindRow(rows, "Freq").Cells[0].Text == "1.000kHz");
    CHECK(FindRow(rows, "Period").Cells[1].Text == "1.000ms");
}

TEST_CASE("A mean that is residue reads ~0 with the exact value as its tooltip", "[statistics_table]") {
    const Waves waves = MakeWaves();
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildTransientStatistics(
        waves.Times, {{.Values = waves.Input, .Unit = "V"}}, {.From = 0.0, .To = STOP_TIME}, std::nullopt);
    const GUI::StatisticsCell &mean = FindRow(rows, "Mean").Cells[0];
    CHECK(mean.Text == "~0V");
    CHECK_FALSE(mean.Tooltip.empty());
}

TEST_CASE("Shown cursors add the value of each trace at both and their difference", "[statistics_table]") {
    const Waves waves = MakeWaves();
    // A quarter period and three quarters: the crest and the trough of the input
    const GUI::PlotCursors cursors = {.Shown = true, .Placed = true, .A = 0.25e-3, .B = 0.75e-3};
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildTransientStatistics(
        waves.Times, {{.Values = waves.Input, .Unit = "V"}}, {.From = cursors.A, .To = cursors.B}, cursors);
    REQUIRE(rows.size() == 10);
    CHECK(FindRow(rows, "At A").Cells[0].Text == "1.000V");
    CHECK(FindRow(rows, "At B").Cells[0].Text == "-1.000V");
    CHECK(FindRow(rows, "B-A").Cells[0].Text == "-2.000V");
}

TEST_CASE("A trace with no points in the span reads - in every measured row", "[statistics_table]") {
    const Waves waves = MakeWaves();
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildTransientStatistics(
        waves.Times, {{.Values = waves.Input, .Unit = "V"}}, {.From = 1.0, .To = 2.0}, std::nullopt);
    for (const GUI::StatisticsRow &row : rows) {
        REQUIRE(row.Cells.size() == 1);
        CHECK(row.Cells[0].Text == "-");
    }
}
