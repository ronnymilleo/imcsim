/**
 * @file    statistics_table_tests.cpp
 * @brief   Tests for the statistics of the plotted traces as tables with a column per trace.
 */

#include "plots/statistics_table.h"

#include "spice/spice_value.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <optional>
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

TEST_CASE("A trace with no points in the span reads a dash in every measured row", "[statistics_table]") {
    const Waves waves = MakeWaves();
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildTransientStatistics(
        waves.Times, {{.Values = waves.Input, .Unit = "V"}}, {.From = 1.0, .To = 2.0}, std::nullopt);
    for (const GUI::StatisticsRow &row : rows) {
        REQUIRE(row.Cells.size() == 1);
        CHECK(row.Cells[0].Text == GUI::NO_VALUE);
    }
}

TEST_CASE("The Bode statistics read across, with the cutoff of each trace in its column", "[statistics_table]") {
    // A first-order low-pass at 1.59 kHz beside a flat trace, from 10 Hz to 1 MHz
    std::vector<double> frequencies;
    std::vector<double> flat_magnitudes;
    std::vector<double> flat_phases;
    std::vector<double> low_pass_magnitudes;
    std::vector<double> low_pass_phases;
    constexpr double CUTOFF = 1591.5;
    // Five decades at 50 points per decade
    constexpr int POINTS = 250;
    for (int point = 0; point <= POINTS; ++point) {
        const double frequency = 10.0 * std::pow(10.0, 5.0 * point / POINTS);
        const double ratio = frequency / CUTOFF;
        frequencies.push_back(frequency);
        flat_magnitudes.push_back(0.0);
        flat_phases.push_back(0.0);
        low_pass_magnitudes.push_back(-10.0 * std::log10(1.0 + ratio * ratio));
        low_pass_phases.push_back(-std::atan(ratio) * 180.0 / std::numbers::pi);
    }
    const std::vector<GUI::StatisticsRow> rows =
        GUI::BuildBodeStatistics(frequencies,
                                 {{.MagnitudesDecibels = flat_magnitudes, .PhasesDegrees = flat_phases},
                                  {.MagnitudesDecibels = low_pass_magnitudes, .PhasesDegrees = low_pass_phases}},
                                 std::nullopt);
    REQUIRE(rows.size() == 6);
    // No space between a figure and its unit, as in every other reading
    CHECK(FindRow(rows, "Peak gain").Cells[0].Text == "0.00dB");
    CHECK(FindRow(rows, "Peak at").Cells[0].Text == "10.00Hz");
    CHECK(FindRow(rows, "-3 dB high").Cells[0].Text == GUI::NO_VALUE);
    // The cutoff is interpolated between sweep points, so it reads close to the exact one, not equal
    const std::optional<double> cutoff = Core::ParseValue(FindRow(rows, "-3 dB high").Cells[1].Text, "Hz");
    REQUIRE(cutoff.has_value());
    CHECK_THAT(*cutoff, Catch::Matchers::WithinRel(CUTOFF, 0.01));
}

TEST_CASE("A swept current reads its slope as a resistance, other curves read -", "[statistics_table]") {
    // The current of a 1k resistor and the voltage across it, swept from 0 V to 10 V
    std::vector<double> swept;
    std::vector<double> currents;
    std::vector<double> voltages;
    for (int step = 0; step <= 100; ++step) {
        const double volts = 0.1 * step;
        swept.push_back(volts);
        currents.push_back(volts / 1e3);
        voltages.push_back(volts);
    }
    const GUI::PlotCursors cursors = {.Shown = true, .Placed = true, .A = 2.0, .B = 8.0};
    const std::vector<GUI::StatisticsRow> rows =
        GUI::BuildSweepStatistics(swept, "V", {{.Values = currents, .Unit = "A"}, {.Values = voltages, .Unit = "V"}},
                                  {.From = 2.0, .To = 8.0}, cursors);
    REQUIRE(rows.size() == 8);
    CHECK(FindRow(rows, "At A").Cells[0].Text == "2.000mA");
    CHECK(FindRow(rows, "Slope").Cells[0].Text == "1.000mA/V");
    CHECK(FindRow(rows, "Resistance").Cells[0].Text == "1.000kOhm");
    CHECK(FindRow(rows, "Resistance").Cells[1].Text == GUI::NO_VALUE);
    CHECK(FindRow(rows, "Max").Cells[1].Text == "8.000V");
}

TEST_CASE("Without cursors a DC sweep reads only its extremes and mean", "[statistics_table]") {
    const std::vector<double> swept = {0.0, 1.0, 2.0};
    const std::vector<double> values = {0.0, 0.5, 1.0};
    const std::vector<GUI::StatisticsRow> rows = GUI::BuildSweepStatistics(
        swept, "V", {{.Values = values, .Unit = "V"}}, {.From = 0.0, .To = 2.0}, std::nullopt);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Name == "Min");
    CHECK(rows[2].Name == "Mean");
}
