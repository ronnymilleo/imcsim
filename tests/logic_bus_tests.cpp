/**
 * @file    logic_bus_tests.cpp
 * @brief   Tests for logic buses: reading several logic signals as one number and writing its value.
 */

#include "plots/logic_buses.h"
#include "plots/plot_export.h"
#include "plots/plot_figures.h"
#include "results/logic_bus.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

/// Resolves the nets Q0 to Q2 to nodes 1 to 3, and nothing else.
std::optional<int> FindCounterNet(const std::string_view net) {
    if (net == "Q0") {
        return 1;
    }
    if (net == "Q1") {
        return 2;
    }
    if (net == "Q2") {
        return 3;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("A bus value is written in binary, hexadecimal or decimal, as wide as its bits ask", "[logic_bus]") {
    CHECK(GUI::FormatBusValue(5, 4, GUI::BusFormat::BINARY) == "0101");
    CHECK(GUI::FormatBusValue(10, 4, GUI::BusFormat::HEXADECIMAL) == "A");
    // Nine bits take three hexadecimal digits, so every value of the bus is as wide
    CHECK(GUI::FormatBusValue(10, 9, GUI::BusFormat::HEXADECIMAL) == "00A");
    CHECK(GUI::FormatBusValue(255, 8, GUI::BusFormat::DECIMAL) == "255");
    CHECK(GUI::FormatBusValue(std::nullopt, 4, GUI::BusFormat::HEXADECIMAL) == "X");
    CHECK(GUI::ParseBusFormat("bin") == GUI::BusFormat::BINARY);
    CHECK(GUI::ParseBusFormat(GUI::GetBusFormatName(GUI::BusFormat::DECIMAL)) == GUI::BusFormat::DECIMAL);
    CHECK_FALSE(GUI::ParseBusFormat("octal"));
}

TEST_CASE("The bits of a bus name their nodes by net or by number, most significant first", "[logic_bus]") {
    const GUI::LogicBus counter{.Name = "COUNT", .Bits = {"V(Q2)", "V(Q1)", "V(Q0)"}};
    CHECK(GUI::ResolveBusBits(counter, FindCounterNet, 4) == std::vector{3, 2, 1});
    const GUI::LogicBus by_number{.Name = "N", .Bits = {"V(3)", "V(1)"}};
    CHECK(GUI::ResolveBusBits(by_number, FindCounterNet, 4) == std::vector{3, 1});

    // A node the result lacks, a net the circuit lacks and no bits at all are refused with the reason
    CHECK(GUI::ResolveBusBits(by_number, FindCounterNet, 3).error().contains("V(3)"));
    const GUI::LogicBus unknown{.Name = "U", .Bits = {"V(OUT)"}};
    CHECK(GUI::ResolveBusBits(unknown, FindCounterNet, 4).error().contains("V(OUT)"));
    CHECK_FALSE(GUI::ResolveBusBits({.Name = "E"}, FindCounterNet, 4));
}

TEST_CASE("A bus keeps a value while all its bits do, and is unknown while any is", "[logic_bus]") {
    const std::vector<double> times = {0.0, 1.0, 2.0, 3.0, 4.0};
    // The high bit is 1 from t = 2, the low bit goes 0 1 1 X 0
    const std::vector<double> high = {0.0, 0.0, 5.0, 5.0, 5.0};
    const std::vector<double> low = {0.0, 5.0, 5.0, 2.5, 0.0};
    const std::vector<GUI::LogicSignal> bits = {{high, 5.0}, {low, 5.0}};
    CHECK(GUI::ReadBusValue(bits, 2) == std::optional<std::uint64_t>(3));
    CHECK_FALSE(GUI::ReadBusValue(bits, 3));

    const std::vector<GUI::BusSegment> segments = GUI::FindBusSegments(times, bits);
    REQUIRE(segments.size() == 5);
    CHECK(segments[0].Value == std::optional<std::uint64_t>(0));
    CHECK(segments[0].To == 1.0);
    CHECK(segments[1].Value == std::optional<std::uint64_t>(1));
    CHECK(segments[2].Value == std::optional<std::uint64_t>(3));
    CHECK(segments[2].From == 2.0);
    CHECK(segments[2].To == 3.0);
    CHECK_FALSE(segments[3].Value);
    CHECK(segments[4].Value == std::optional<std::uint64_t>(2));
    CHECK(segments[4].To == 4.0);
    CHECK(GUI::FindBusSegments({}, bits).empty());
}

TEST_CASE("A new bus gets a name no other bus has", "[logic_bus]") {
    CHECK(GUI::GetNextBusName({}) == "BUS1");
    CHECK(GUI::GetNextBusName({{.Name = "BUS1"}, {.Name = "BUS3"}}) == "BUS2");
    CHECK(GUI::DescribeBus({.Name = "B", .Bits = {"V(Q1)", "V(Q0)"}, .Format = GUI::BusFormat::BINARY}) ==
          "V(Q1) V(Q0), bin");
    CHECK(GUI::DescribeBus({.Name = "E"}) == "no bits, hex");
}

TEST_CASE("Buses are saved with the measurements of a file and read back", "[logic_bus]") {
    GUI::SavedMeasurements measurements;
    measurements.Buses = {
        {.Name = "COUNT", .Bits = {"V(Q2)", "V(Q1)", "V(Q0)"}, .Format = GUI::BusFormat::DECIMAL, .Shown = false}};
    const std::string text = GUI::SaveSchematic({}, {}, {}, measurements);
    const auto loaded = GUI::LoadSchematic(text);
    REQUIRE(loaded);
    CHECK(loaded->Measurements.Buses == measurements.Buses);

    // A bus with a format it does not know is skipped with a warning
    std::string damaged = text;
    damaged.replace(damaged.find("\"dec\""), 5, "\"oct\"");
    const auto read = GUI::LoadSchematic(damaged);
    REQUIRE(read);
    CHECK(read->Measurements.Buses.empty());
    CHECK_FALSE(read->Warnings.empty());

    GUI::Schematic schematic;
    schematic.SetBuses(measurements.Buses);
    CHECK(schematic.SaveMeasurements().Buses == measurements.Buses);
    schematic.ClearMeasurements();
    CHECK(schematic.GetBuses().empty());
}

TEST_CASE("A plot draws the shown buses whose bits all name a node, read against their logic levels", "[logic_bus]") {
    const std::vector<GUI::LogicBus> buses = {
        {.Name = "COUNT", .Bits = {"V(Q1)", "V(Q0)"}},
        {.Name = "HIDDEN", .Bits = {"V(Q0)"}, .Shown = false},
        {.Name = "BROKEN", .Bits = {"V(OUT)"}},
    };
    // Q0 is the output of a 3.3 V part; Q1 is on no logic part
    const std::vector<GUI::LogicNode> logic_nodes = {{.Node = 1, .LogicLevel = 3.3}};
    const std::vector<GUI::BusLane> lanes = GUI::ResolveBusLanes(buses, logic_nodes, FindCounterNet, 4);
    REQUIRE(lanes.size() == 1);
    CHECK(lanes[0].Name == "COUNT");
    REQUIRE(lanes[0].Bits.size() == 2);
    CHECK(lanes[0].Bits[0].Node == 2);
    CHECK(lanes[0].Bits[0].LogicLevel == 5.0);
    CHECK(lanes[0].Bits[1].LogicLevel == 3.3);
}

TEST_CASE("The outline of a bus pinches to the middle of its lane where the value changes", "[logic_bus]") {
    const std::vector<double> bit = {0.0, 0.0, 5.0, 2.5};
    const std::vector<GUI::LogicSignal> bits = {{bit, 5.0}};
    // The second lane from the bottom
    const GUI::BusOutline outline = GUI::BuildBusOutline(bits, bit.size(), 1);
    const double base = GUI::LANE_STRIDE;
    const double middle = base + (GUI::LANE_LOW_OFFSET + GUI::LANE_HIGH_OFFSET) / 2.0;
    CHECK(outline.Top == std::vector{base + GUI::LANE_HIGH_OFFSET, base + GUI::LANE_HIGH_OFFSET, middle, middle});
    CHECK(outline.Bottom[1] == base + GUI::LANE_LOW_OFFSET);
    CHECK(outline.Bottom[2] == middle);
    CHECK(outline.Values[2] == 1.0);
    CHECK(std::isnan(outline.Values[3]));
}

TEST_CASE("An exported bus lane writes its values in the image and as numbers in the table", "[logic_bus]") {
    Core::Transient transient;
    transient.Times = {0.0, 1.0, 2.0, 3.0};
    // Node 1 is the low bit and node 2 the high one, counting 0 1 2 3
    transient.NodeVoltages = {{0.0, 0.0, 0.0, 0.0}, {0.0, 5.0, 0.0, 5.0}, {0.0, 0.0, 5.0, 5.0}};
    const GUI::BusLane lane{.Name = "COUNT",
                            .Bits = {{.Node = 2, .LogicLevel = 5.0}, {.Node = 1, .LogicLevel = 5.0}},
                            .Format = GUI::BusFormat::DECIMAL};
    GUI::Schematic schematic;
    const GUI::ExportFigure figure =
        GUI::BuildTransientFigure(transient, {}, {}, {lane}, schematic, false, {.From = 0.0, .To = 3.0});
    // Nothing analog is measured, so the lanes are the only panel
    REQUIRE(figure.Panels.size() == 1);
    const GUI::ExportPanel &panel = figure.Panels[0];
    REQUIRE(panel.Series.size() == 2);
    CHECK_FALSE(panel.Series[1].InTable);
    std::vector<std::string> notes;
    for (const GUI::ExportNote &note : panel.Notes) {
        notes.push_back(note.Text);
    }
    CHECK(notes == std::vector<std::string>{"0", "1", "2"});

    const std::string csv = GUI::RenderCsv(figure);
    // One column for the bus, with no unit, and the values as numbers
    CHECK(csv.starts_with("Time (s),COUNT\n"));
    CHECK(csv.contains("\n2,2\n"));
}
