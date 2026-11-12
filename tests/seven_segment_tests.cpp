/**
 * @file    seven_segment_tests.cpp
 * @brief   Tests for the 7-segment display, which lights the segments whose inputs read 1 and takes no part in the
 *          simulation.
 */

#include "analysis/logic_outputs.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/seven_segment.h"
#include "document/schematic.h"
#include "document/schematic_file.h"
#include "editing/element_factory.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace {

/// Where the display goes; its inputs are three units to the left of it.
constexpr GUI::GridPoint DISPLAY_POSITION = {10, 0};

/**
 * @brief   A display with a logic input on each of its inputs, high for the segments the text names.
 */
GUI::Schematic MakeDrivenDisplay(const std::string_view lit_segments) {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::SEVEN_SEGMENT, DISPLAY_POSITION, GUI::Rotation::R0));
    const std::vector<GUI::GridPoint> inputs = schematic.GetElements().front()->GetTerminals();
    for (std::size_t segment = 0; segment < inputs.size(); ++segment) {
        auto driver = GUI::CreateElement(Core::ComponentType::LOGIC_INPUT, inputs[segment], GUI::Rotation::R0);
        const bool is_lit = lit_segments.contains(Core::SEGMENT_NAMES[segment][0]);
        static_cast<Core::LogicInput &>(driver->GetComponent()).SetHigh(is_lit);
        schematic.AddElement(std::move(driver));
    }
    return schematic;
}

} // namespace

TEST_CASE("A 7-segment display has no name, no value and a logic level to read against", "[seven_segment]") {
    const Core::SevenSegmentDisplay display;
    CHECK(display.GetType() == Core::ComponentType::SEVEN_SEGMENT);
    CHECK(std::string_view(display.GetNamePrefix()).empty());
    CHECK_FALSE(display.HasValue());
    CHECK(display.GetParameters().LogicLevel == 5.0);
    CHECK_FALSE(Core::SevenSegmentDisplay::IsValidParameters({.LogicLevel = 0.0}));
    CHECK(Core::ParseComponentType("SevenSegment") == Core::ComponentType::SEVEN_SEGMENT);
    CHECK(display.Clone()->GetType() == Core::ComponentType::SEVEN_SEGMENT);
    CHECK(Core::IsLogicDisplay(Core::ComponentType::SEVEN_SEGMENT));
    CHECK(std::string_view(GUI::GetPartName(Core::ComponentType::SEVEN_SEGMENT)) == "7-segment display");
    CHECK(std::ranges::contains(GUI::FindParts("segment"), Core::ComponentType::SEVEN_SEGMENT));
}

TEST_CASE("A 7-segment display has its inputs a to g down its left side", "[seven_segment]") {
    const auto display = GUI::CreateElement(Core::ComponentType::SEVEN_SEGMENT, DISPLAY_POSITION, GUI::Rotation::R0);
    const std::vector<GUI::GridPoint> inputs = display->GetTerminals();
    REQUIRE(inputs.size() == Core::SEGMENT_NAMES.size());
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        CAPTURE(input);
        CHECK(inputs[input].X == DISPLAY_POSITION.X - 3);
        // From a at the top to g at the bottom, two units apart
        if (input > 0) {
            CHECK(inputs[input].Y == inputs[input - 1].Y + 2);
        }
        CHECK(display->GetTerminalInward(input) == GUI::GridPoint{1, 0});
    }
}

TEST_CASE("A 7-segment display stays out of the circuit, which needs no Ground for it", "[seven_segment]") {
    GUI::Schematic schematic = MakeDrivenDisplay("bc");
    const Core::Circuit circuit = schematic.BuildCircuit();
    CHECK(circuit.GetEntries().size() == Core::SEGMENT_NAMES.size());
    CHECK(std::ranges::none_of(circuit.GetEntries(), [](const auto &entry) {
        return entry.Part->GetType() == Core::ComponentType::SEVEN_SEGMENT;
    }));
    CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
}

TEST_CASE("A 7-segment display reads every input against its level, in segment order", "[seven_segment]") {
    // The segments of a 2
    GUI::Schematic schematic = MakeDrivenDisplay("abdeg");
    const Core::OperatingPointRun run = Core::RunOperatingPoint(schematic.BuildCircuit());
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    const GUI::LogicState high = GUI::LogicState::HIGH;
    const GUI::LogicState low = GUI::LogicState::LOW;
    CHECK(GUI::ReadLogicDisplay(*schematic.GetElements().front(), schematic.GetConnectivity(), *run.Result) ==
          std::vector<std::optional<GUI::LogicState>>{high, high, low, high, high, low, high});

    // Against a 10 V level, the 5 V inputs are between the levels
    static_cast<Core::SevenSegmentDisplay &>(schematic.GetElement(0).GetComponent())
        .SetParameters({.LogicLevel = 10.0});
    const auto readings =
        GUI::ReadLogicDisplay(*schematic.GetElements().front(), schematic.GetConnectivity(), *run.Result);
    CHECK(readings[0] == GUI::LogicState::UNKNOWN);
    CHECK(readings[2] == low);
}

TEST_CASE("Only logic displays have readings", "[seven_segment]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    CHECK(GUI::ReadLogicDisplay(*schematic.GetElements().front(), schematic.GetConnectivity(), {}).empty());
}

TEST_CASE("A 7-segment display keeps its logic level in a file", "[seven_segment]") {
    GUI::Schematic schematic = MakeDrivenDisplay("");
    static_cast<Core::SevenSegmentDisplay &>(schematic.GetElement(0).GetComponent()).SetParameters({.LogicLevel = 3.3});
    const auto loaded = GUI::LoadSchematic(GUI::SaveSchematic(schematic.GetElements(), schematic.GetWires(), {}, {}));
    REQUIRE(loaded);
    const auto &display = static_cast<const Core::SevenSegmentDisplay &>(loaded->Elements[0]->GetComponent());
    CHECK(display.GetType() == Core::ComponentType::SEVEN_SEGMENT);
    CHECK(display.GetParameters().LogicLevel == 3.3);
}
