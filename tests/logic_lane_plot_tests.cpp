/**
 * @file    logic_lane_plot_tests.cpp
 * @brief   Tests for which logic lanes the Transient tab plots and how it names them.
 */

#include "windows/output_tabs/logic_lane_plot.h"

#include "schematic/examples.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

namespace {

/// Node count large enough for every node of the example.
constexpr std::size_t NODE_COUNT = 1000;

void OpenCounter(GUI::Schematic &schematic) {
    const auto example = std::ranges::find(GUI::GetExamples(), "counter_8bit_blocks.imcsim", &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded);
    schematic.Replace(std::move(loaded->Elements),
                      std::move(loaded->Wires),
                      std::move(loaded->Settings),
                      std::move(loaded->Subcircuits),
                      std::move(loaded->Models));
    schematic.RestoreMeasurements(loaded->Measurements);
}

} // namespace

TEST_CASE("A circuit without logic parts has no lanes to offer", "[logic_lane_plot]") {
    GUI::Schematic schematic;
    const GUI::LaneLayout layout = GUI::FindLaneLayout(schematic, NODE_COUNT, true);
    CHECK_FALSE(layout.HasLogicParts);
    CHECK(layout.GetCount() == 0);
}

TEST_CASE("Logic lanes turned off show none, though the circuit still offers them", "[logic_lane_plot]") {
    GUI::Schematic schematic;
    OpenCounter(schematic);
    const GUI::LaneLayout shown = GUI::FindLaneLayout(schematic, NODE_COUNT, true);
    CHECK(shown.HasLogicParts);
    REQUIRE(shown.GetCount() > 0);
    // The bus of the counter takes the top lane and is named after itself
    REQUIRE(shown.Buses.size() == 1);
    CHECK(GUI::ListLaneNames(shown, schematic.GetConnectivity()).front() == shown.Buses.front().Name);

    const GUI::LaneLayout hidden = GUI::FindLaneLayout(schematic, NODE_COUNT, false);
    CHECK(hidden.HasLogicParts);
    CHECK(hidden.GetCount() == 0);
}
