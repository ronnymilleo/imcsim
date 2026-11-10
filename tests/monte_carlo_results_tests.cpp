/**
 * @file    monte_carlo_results_tests.cpp
 * @brief   Tests for what a Monte Carlo analysis keeps of its runs: measurements, statistics, histogram, ordering and
 *          the CSV table.
 */

#include "analysis/monte_carlo_results.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace {

/// A table of the given runs, each measured by one column, and one varied part whose value is the run number.
GUI::MonteCarloTable MakeTable(const std::vector<std::optional<double>> &measures) {
    GUI::MonteCarloTable table;
    table.Parts = {"R1"};
    table.Units = {"Ohm"};
    table.Columns = {{.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF}};
    table.RequestedRuns = measures.size() - 1;
    for (std::size_t run = 0; run < measures.size(); ++run) {
        table.Runs.push_back({.Values = {1e3 + static_cast<double>(run)}, .Measures = {measures[run]}});
    }
    return table;
}

} // namespace

TEST_CASE("A transient is measured over its whole run for every node but ground", "[monte_carlo_results]") {
    Core::Transient transient;
    for (int step = 0; step <= 1000; ++step) {
        transient.Times.push_back(static_cast<double>(step) * 1e-5);
    }
    transient.NodeVoltages.assign(3, {});
    for (const double time : transient.Times) {
        transient.NodeVoltages[0].push_back(0.0);
        transient.NodeVoltages[1].push_back(2.0 + std::sin(2.0 * std::numbers::pi * 1e3 * time));
        transient.NodeVoltages[2].push_back(1.0);
    }
    const GUI::Measurement measured = GUI::MeasureTransient(transient);
    REQUIRE(measured.Columns.size() == 8);
    CHECK(measured.Columns[0] == GUI::MeasureColumn{.Node = 1, .Kind = GUI::MeasureKind::MEAN});
    CHECK(measured.Columns[4].Node == 2);
    REQUIRE(measured.Values[0]);
    CHECK_THAT(*measured.Values[0], Catch::Matchers::WithinAbs(2.0, 0.02));
    // The sine peaks 1 V either side of its mean
    CHECK_THAT(*measured.Values[2], Catch::Matchers::WithinAbs(2.0, 0.02));
    CHECK(measured.Values[3]);
    CHECK_THAT(*measured.Values[4], Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("An AC sweep is measured by its Bode statistics, the upper cutoff first", "[monte_carlo_results]") {
    constexpr double CORNER = 1e3;
    Core::ACSweep sweep;
    sweep.NodeMagnitudesDecibels.assign(2, {});
    sweep.NodePhasesDegrees.assign(2, {});
    for (int index = 0; index <= 60; ++index) {
        const double frequency = std::pow(10.0, 1.0 + index * 0.1);
        sweep.Frequencies.push_back(frequency);
        const double ratio = frequency / CORNER;
        sweep.NodeMagnitudesDecibels[1].push_back(-10.0 * std::log10(1.0 + ratio * ratio));
        sweep.NodePhasesDegrees[1].push_back(-std::atan(ratio) * 180.0 / std::numbers::pi);
    }
    const GUI::Measurement measured = GUI::MeasureACSweep(sweep);
    REQUIRE(measured.Columns.size() == 5);
    CHECK(measured.Columns[0].Kind == GUI::MeasureKind::UPPER_CUTOFF);
    REQUIRE(measured.Values[0]);
    CHECK_THAT(*measured.Values[0], Catch::Matchers::WithinRel(CORNER, 0.02));
    // The response starts at 0 dB, so there is no lower cutoff
    CHECK_FALSE(measured.Values[1]);
}

TEST_CASE("A DC sweep is measured by the extremes and the mean of its first curve", "[monte_carlo_results]") {
    Core::DCSweep sweep;
    sweep.SweptValues = {0.0, 1.0, 2.0, 3.0, 4.0};
    Core::DCSweepCurve curve;
    curve.NodeVoltages = {{0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 2.0, 3.0, 4.0}};
    sweep.Curves = {curve};
    const GUI::Measurement measured = GUI::MeasureDCSweep(sweep);
    REQUIRE(measured.Columns.size() == 3);
    CHECK(measured.Values[0] == 0.0);
    CHECK(measured.Values[1] == 4.0);
    REQUIRE(measured.Values[2]);
    CHECK_THAT(*measured.Values[2], Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("An operating point is measured by the DC value of every node and every current", "[monte_carlo_results]") {
    const Core::OperatingPoint point{.NodeVoltages = {0.0, 5.0, 2.5},
                                     .Currents = {{.Name = "R1", .Current = 2.5e-3}, {.Name = "Q1.C", .Current = 1e-3}},
                                     .Warnings = {}};
    const GUI::Measurement measured = GUI::MeasureOperatingPoint(point);
    REQUIRE(measured.Columns.size() == 4);
    // Ground is left out, as for the other analyses
    CHECK(measured.Columns[0] == GUI::MeasureColumn{.Node = 1, .Kind = GUI::MeasureKind::DC_VOLTAGE, .Current = ""});
    CHECK(measured.Values[1] == 2.5);
    CHECK(measured.Columns[2].Current == "R1");
    CHECK(measured.Columns[2].Kind == GUI::MeasureKind::DC_CURRENT);
    CHECK(measured.Values[3] == 1e-3);
}

TEST_CASE("Measured quantities have names and units", "[monte_carlo_results]") {
    CHECK(std::string(GUI::GetMeasureName(GUI::MeasureKind::UPPER_CUTOFF)) == "Upper -3 dB frequency");
    CHECK(std::string(GUI::GetMeasureUnit(GUI::MeasureKind::UPPER_CUTOFF)) == "Hz");
    CHECK(std::string(GUI::GetMeasureUnit(GUI::MeasureKind::PEAK_GAIN)) == "dB");
    CHECK(std::string(GUI::GetMeasureUnit(GUI::MeasureKind::MEAN)) == "V");
    CHECK(std::string(GUI::GetMeasureName(GUI::MeasureKind::DC_CURRENT)) == "DC current");
    CHECK(std::string(GUI::GetMeasureUnit(GUI::MeasureKind::DC_CURRENT)) == "A");
    CHECK(std::string(GUI::GetMeasureUnit(GUI::MeasureKind::DC_VOLTAGE)) == "V");
}

TEST_CASE("The samples of a column leave out the nominal run, failed runs and missing readings",
          "[monte_carlo_results]") {
    GUI::MonteCarloTable table = MakeTable({10.0, 1.0, std::nullopt, 3.0, 4.0});
    table.Runs[4].IsFailed = true;
    CHECK(GUI::CollectMeasure(table, 0) == std::vector<double>{1.0, 3.0});
}

TEST_CASE("Statistics give the mean, the sample deviation, the extremes and the percentiles", "[monte_carlo_results]") {
    std::vector<double> samples;
    for (int value = 1; value <= 101; ++value) {
        samples.push_back(static_cast<double>(value));
    }
    const std::optional<GUI::SampleStatistics> statistics = GUI::ComputeSampleStatistics(samples);
    REQUIRE(statistics);
    CHECK(statistics->Count == 101);
    CHECK(statistics->Minimum == 1.0);
    CHECK(statistics->Maximum == 101.0);
    CHECK_THAT(statistics->Mean, Catch::Matchers::WithinAbs(51.0, 1e-9));
    // The deviation of 1..n with n - 1 in the divisor is the root of n (n + 1) / 12
    CHECK_THAT(statistics->StandardDeviation, Catch::Matchers::WithinAbs(std::sqrt(101.0 * 102.0 / 12.0), 1e-9));
    CHECK_THAT(statistics->Percentile1, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(statistics->Percentile99, Catch::Matchers::WithinAbs(100.0, 1e-9));
}

TEST_CASE("Statistics of one sample have no spread and of none are missing", "[monte_carlo_results]") {
    const std::vector<double> one = {5.0};
    const std::optional<GUI::SampleStatistics> single = GUI::ComputeSampleStatistics(one);
    REQUIRE(single);
    CHECK(single->StandardDeviation == 0.0);
    CHECK(single->Percentile1 == 5.0);
    CHECK_FALSE(GUI::ComputeSampleStatistics({}));
}

TEST_CASE("The percentiles interpolate between samples", "[monte_carlo_results]") {
    const std::vector<double> samples = {0.0, 10.0};
    const std::optional<GUI::SampleStatistics> statistics = GUI::ComputeSampleStatistics(samples);
    REQUIRE(statistics);
    CHECK_THAT(statistics->Percentile1, Catch::Matchers::WithinAbs(0.1, 1e-9));
    CHECK_THAT(statistics->Percentile99, Catch::Matchers::WithinAbs(9.9, 1e-9));
}

TEST_CASE("A histogram counts every sample once, with the largest in the last bin", "[monte_carlo_results]") {
    const std::vector<double> samples = {0.0, 1.0, 1.0, 2.0, 3.0, 4.0};
    const GUI::Histogram histogram = GUI::BuildHistogram(samples, 4);
    CHECK(histogram.From == 0.0);
    CHECK(histogram.To == 4.0);
    CHECK(histogram.Counts == std::vector<std::size_t>{1, 2, 1, 2});
}

TEST_CASE("A histogram of equal samples is widened around them", "[monte_carlo_results]") {
    const std::vector<double> samples = {5.0, 5.0, 5.0};
    const GUI::Histogram histogram = GUI::BuildHistogram(samples, 5);
    CHECK(histogram.From < 5.0);
    CHECK(histogram.To > 5.0);
    std::size_t total = 0;
    for (const std::size_t count : histogram.Counts) {
        total += count;
    }
    CHECK(total == 3);
    CHECK(GUI::BuildHistogram({}, 5).Counts.empty());
}

TEST_CASE("The bin count follows the root of the sample count within limits", "[monte_carlo_results]") {
    CHECK(GUI::SuggestBinCount(4) == 5);
    CHECK(GUI::SuggestBinCount(100) == 10);
    CHECK(GUI::SuggestBinCount(1000000) == 40);
}

TEST_CASE("The runs sort by number, part value or measurement with the nominal one first", "[monte_carlo_results]") {
    GUI::MonteCarloTable table = MakeTable({0.0, 30.0, 10.0, std::nullopt, 20.0});
    CHECK(GUI::OrderRuns(table, {}) == std::vector<std::size_t>{0, 1, 2, 3, 4});
    CHECK(GUI::OrderRuns(table, {.By = GUI::RunSort::Key::RUN, .IsAscending = false}) ==
          std::vector<std::size_t>{0, 4, 3, 2, 1});
    CHECK(GUI::OrderRuns(table, {.By = GUI::RunSort::Key::MEASURE, .Index = 0}) ==
          std::vector<std::size_t>{0, 2, 4, 1, 3});
    // A run without the reading is last in both directions
    CHECK(GUI::OrderRuns(table, {.By = GUI::RunSort::Key::MEASURE, .Index = 0, .IsAscending = false}) ==
          std::vector<std::size_t>{0, 1, 4, 2, 3});
    CHECK(GUI::OrderRuns(table, {.By = GUI::RunSort::Key::PART, .Index = 0, .IsAscending = false}) ==
          std::vector<std::size_t>{0, 4, 3, 2, 1});
}

TEST_CASE("The nodes and columns of a table are listed", "[monte_carlo_results]") {
    GUI::MonteCarloTable table;
    table.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::UPPER_CUTOFF},
                     {.Node = 1, .Kind = GUI::MeasureKind::PEAK_GAIN},
                     {.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF},
                     {.Node = 2, .Kind = GUI::MeasureKind::PEAK_GAIN}};
    CHECK(GUI::ListTableNodes(table) == std::vector<int>{1, 2});
    CHECK(GUI::ListNodeColumns(table, 2) == std::vector<std::size_t>{2, 3});
}

TEST_CASE("The currents of a table are listed apart from its nodes", "[monte_carlo_results]") {
    GUI::MonteCarloTable table;
    table.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::DC_VOLTAGE},
                     {.Node = 0, .Kind = GUI::MeasureKind::DC_CURRENT, .Current = "R1"},
                     {.Node = 0, .Kind = GUI::MeasureKind::DC_CURRENT, .Current = "C1"}};
    // A current column takes no node, so node 0 is not listed for it
    CHECK(GUI::ListTableNodes(table) == std::vector<int>{1});
    CHECK(GUI::ListNodeColumns(table, 0).empty());
    CHECK(GUI::ListTableCurrents(table) == std::vector<std::string>{"R1", "C1"});
    CHECK(GUI::ListCurrentColumns(table, "C1") == std::vector<std::size_t>{2});
    CHECK(GUI::ListCurrentColumns(table, "R9").empty());
}

TEST_CASE("Samples that differ only by rounding noise have no spread", "[monte_carlo_results]") {
    constexpr double FLOOR = 1e-3;
    CHECK(GUI::ComputeRelativeSpread(std::vector<double>{1.0}, FLOOR) == 0.0);
    CHECK(GUI::ComputeRelativeSpread(std::vector<double>{5.0, 5.0, 5.0}, FLOOR) == 0.0);
    // 0 dB read as a few femtodecibels
    CHECK(GUI::ComputeRelativeSpread(std::vector<double>{-2e-15, 3e-15, 0.0}, FLOOR) == 0.0);
    CHECK(GUI::ComputeRelativeSpread(std::vector<double>{1500.0, 1500.0 * (1.0 + 1e-9)}, FLOOR) == 0.0);
    // Real but too small to read: the -0.0001 dB of a filter far below its corner
    CHECK(GUI::ComputeRelativeSpread(std::vector<double>{-1e-4, -1.2e-4}, FLOOR) == 0.0);
    // A spread of 1 in 10 over the size of the samples
    CHECK_THAT(GUI::ComputeRelativeSpread(std::vector<double>{90.0, 100.0}, FLOOR),
               Catch::Matchers::WithinAbs(std::sqrt(50.0) / 100.0, 1e-12));
}

TEST_CASE("The quantity shown first is the one that spreads most, whatever the node", "[monte_carlo_results]") {
    GUI::MonteCarloTable table;
    table.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::UPPER_CUTOFF},
                     {.Node = 1, .Kind = GUI::MeasureKind::PEAK_GAIN},
                     {.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF},
                     {.Node = 2, .Kind = GUI::MeasureKind::PEAK_GAIN}};
    // The input has a flat gain of 0 dB and no cutoff; the output has a cutoff that moves and a gain that does not
    table.Runs = {{.Measures = {std::nullopt, 0.0, 1600.0, -1e-15}},
                  {.Measures = {std::nullopt, 1e-15, 1500.0, 0.0}},
                  {.Measures = {std::nullopt, 0.0, 1700.0, 2e-15}}};
    const std::vector<int> input_last = {2, 1};
    const std::vector<int> input_first = {1, 2};
    CHECK(GUI::FindDefaultColumn(table, input_last) == 2);
    CHECK(GUI::FindDefaultColumn(table, input_first) == 2);
}

TEST_CASE("When nothing spreads the first quantity of the node measured last is shown", "[monte_carlo_results]") {
    GUI::MonteCarloTable table;
    table.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::PEAK_GAIN},
                     {.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF},
                     {.Node = 2, .Kind = GUI::MeasureKind::PEAK_GAIN}};
    table.Runs = {{.Measures = {0.0, 1600.0, 0.0}}, {.Measures = {0.0, 1600.0, 0.0}}, {.Measures = {0.0, 1600.0, 0.0}}};
    const std::vector<int> nodes = {1, 2};
    CHECK(GUI::FindDefaultColumn(table, nodes) == 1);
    const std::vector<int> reversed = {2, 1};
    CHECK(GUI::FindDefaultColumn(table, reversed) == 0);
    // A column that no run could read ranks below all
    table.Runs = {{.Measures = {0.0, std::nullopt, 0.0}}, {.Measures = {0.0, std::nullopt, 0.0}}};
    CHECK(GUI::FindDefaultColumn(table, nodes) == 2);
}

TEST_CASE("The smallest readable difference follows the unit of the quantity", "[monte_carlo_results]") {
    CHECK(GUI::GetMeasureFloor(GUI::MeasureKind::MEAN) == 1e-6);
    CHECK(GUI::GetMeasureFloor(GUI::MeasureKind::UPPER_CUTOFF) == 1e-3);
    CHECK(GUI::GetMeasureFloor(GUI::MeasureKind::PEAK_GAIN) == 1e-3);
    CHECK(GUI::GetMeasureFloor(GUI::MeasureKind::PHASE_MARGIN) == 1e-3);
    CHECK(GUI::GetMeasureFloor(GUI::MeasureKind::DC_CURRENT) == 1e-9);
}

TEST_CASE("A reading every run gives alike, and runs without a reading, are worded", "[monte_carlo_results]") {
    GUI::MonteCarloTable table;
    table.Columns = {{.Node = 1, .Kind = GUI::MeasureKind::PEAK_GAIN},
                     {.Node = 2, .Kind = GUI::MeasureKind::UPPER_CUTOFF}};
    table.Runs = {{.Measures = {0.0, 1600.0}},
                  {.Measures = {1e-15, 1500.0}},
                  {.Measures = {-1e-15, std::nullopt}},
                  {.Measures = {0.0, std::nullopt}},
                  {.IsFailed = true}};
    CHECK(GUI::DescribeUniformReading(table, 0) == "Every run gives 0dB");
    CHECK_FALSE(GUI::DescribeUnreadRuns(table, 0));
    CHECK(GUI::DescribeUnreadRuns(table, 1) == "Upper -3 dB frequency not reached in 2 runs");
    table.Runs.pop_back();
    table.Runs.pop_back();
    CHECK(GUI::DescribeUnreadRuns(table, 1) == "Upper -3 dB frequency not reached in 1 run");
}

TEST_CASE("Failed runs and a stop before the end are reported", "[monte_carlo_results]") {
    GUI::MonteCarloTable table = MakeTable({0.0, 1.0, 2.0, 3.0});
    CHECK(GUI::ListMonteCarloWarnings(table).empty());
    table.Runs[2].IsFailed = true;
    table.Runs[3].IsFailed = true;
    table.RequestedRuns = 5;
    const std::vector<std::string> warnings = GUI::ListMonteCarloWarnings(table);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == "2 of 3 runs failed and are left out of the statistics: 2, 3");
    CHECK(warnings[1] == "Stopped after 3 of 5 runs; the statistics cover those");
}

TEST_CASE("A long list of failed runs names the first ones and counts the rest", "[monte_carlo_results]") {
    std::vector<std::optional<double>> measures(1, 0.0);
    measures.resize(16, std::nullopt);
    GUI::MonteCarloTable table = MakeTable(measures);
    for (std::size_t run = 1; run < table.Runs.size(); ++run) {
        table.Runs[run].IsFailed = true;
    }
    const std::vector<std::string> warnings = GUI::ListMonteCarloWarnings(table);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("15 of 15 runs failed"));
    CHECK(warnings[0].contains("and 5 more"));
}

TEST_CASE("The CSV has a row per run with the values, the measurements and the status", "[monte_carlo_results]") {
    GUI::MonteCarloTable table = MakeTable({1600.0, 1500.5, std::nullopt});
    table.Runs[2].IsFailed = true;
    table.Runs[2].Error = "no convergence, sorry";
    const std::string csv = GUI::WriteMonteCarloCsv(table, {"V(OUT) Upper -3 dB frequency (Hz)"});
    CHECK(csv == "Run,R1 (Ohm),V(OUT) Upper -3 dB frequency (Hz),Status\n"
                 "Nominal,1000,1600,ok\n"
                 "1,1001,1500.5,ok\n"
                 "2,1002,,\"no convergence, sorry\"\n");
}
