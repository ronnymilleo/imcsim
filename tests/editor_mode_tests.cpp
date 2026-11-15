/**
 * @file    editor_mode_tests.cpp
 * @brief   Tests for the modes of the editor: what each one shows in the status bar, placing and drawing wires.
 */

#include "schematic/editor_mode.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("Only the selecting mode selects, and only the placing mode names a part", "[editor_mode]") {
    CHECK(GUI::IsSelecting(GUI::SelectingMode{}));
    CHECK_FALSE(GUI::IsSelecting(GUI::WiringMode{}));
    CHECK_FALSE(GUI::IsSelecting(GUI::ProbingMode{}));
    CHECK_FALSE(GUI::IsSelecting(GUI::PlacingMode{.Type = Core::ComponentType::CAPACITOR}));

    CHECK(GUI::GetPlacingType(GUI::PlacingMode{.Type = Core::ComponentType::CAPACITOR}) ==
          Core::ComponentType::CAPACITOR);
    CHECK_FALSE(GUI::GetPlacingType(GUI::SelectingMode{}));
    CHECK_FALSE(GUI::GetPlacingType(GUI::WiringMode{}));
}

TEST_CASE("Every mode but selecting tells its keys in the status bar", "[editor_mode]") {
    CHECK_FALSE(GUI::DescribeMode(GUI::SelectingMode{}));
    const auto placing = GUI::DescribeMode(GUI::PlacingMode{.Type = Core::ComponentType::RESISTOR});
    REQUIRE(placing);
    CHECK(placing->starts_with("Placing Resistor: click to place"));
    CHECK(GUI::DescribeMode(GUI::PastingMode{})->starts_with("Pasting:"));
    CHECK(GUI::DescribeMode(GUI::ProbingMode{})->starts_with("Probe:"));
    // A wire in progress takes other clicks than the first one
    CHECK(GUI::DescribeMode(GUI::WiringMode{}) == "Wire: click to start, Esc or right click to stop");
    CHECK(GUI::DescribeMode(GUI::WiringMode{.Start = GUI::GridPoint{1, 2}})->starts_with("Wire: click to bend"));
}

TEST_CASE("The hint of the selecting mode follows the live run and what is selected", "[editor_mode]") {
    // A live run takes the clicks, whatever is selected
    CHECK(GUI::DescribeSelecting(true, "R1", 1).starts_with("Live:"));
    CHECK(GUI::DescribeSelecting(false, "R1", 1).starts_with("R1 selected:"));
    CHECK(GUI::DescribeSelecting(false, std::nullopt, 1).starts_with("Wire selected:"));
    CHECK(GUI::DescribeSelecting(false, std::nullopt, 3).starts_with("3 items selected:"));
    CHECK(GUI::DescribeSelecting(false, std::nullopt, 0).starts_with("Click a part or wire to select it"));
}

TEST_CASE("R turns the part being placed, M mirrors it and Shift+M flips it upside down", "[editor_mode]") {
    GUI::PlacingMode mode{.Type = Core::ComponentType::DIODE};
    GUI::TurnPlacement(mode);
    CHECK(mode.Orientation == GUI::Rotation::R90);
    GUI::MirrorPlacement(mode, false);
    CHECK(mode.IsMirrored);
    CHECK(mode.Orientation == GUI::Rotation::R90);
    // A flip top to bottom is a mirror and a half turn
    GUI::MirrorPlacement(mode, true);
    CHECK_FALSE(mode.IsMirrored);
    CHECK(mode.Orientation == GUI::Rotation::R270);
}

TEST_CASE("A wire bends where the direction of its first piece says", "[editor_mode]") {
    CHECK(GUI::FindWireCorner({0, 0}, {4, 3}, false) == GUI::GridPoint{4, 0});
    CHECK(GUI::FindWireCorner({0, 0}, {4, 3}, true) == GUI::GridPoint{0, 3});
}

TEST_CASE("Each click on the way adds a bend up to the cursor, and a terminal ends the wire", "[editor_mode]") {
    GUI::WiringMode mode;
    // The first click only starts the wire
    CHECK(GUI::ClickWire(mode, {0, 0}, false, false).empty());
    CHECK(mode.Start == GUI::GridPoint{0, 0});

    const std::vector<GUI::UIWire> bend = GUI::ClickWire(mode, {4, 3}, false, false);
    REQUIRE(bend.size() == 2);
    CHECK(bend[0].GetStart() == GUI::GridPoint{0, 0});
    CHECK(bend[0].GetEnd() == GUI::GridPoint{4, 0});
    CHECK(bend[1].GetStart() == GUI::GridPoint{4, 0});
    CHECK(bend[1].GetEnd() == GUI::GridPoint{4, 3});
    CHECK(mode.Start == GUI::GridPoint{4, 3});

    CHECK(GUI::ClickWire(mode, {8, 3}, false, true).size() == 2);
    CHECK_FALSE(mode.Start);
}

TEST_CASE("A second click on the point a wire goes on from ends it without a piece", "[editor_mode]") {
    GUI::WiringMode mode{.Start = GUI::GridPoint{2, 2}};
    CHECK(GUI::ClickWire(mode, {2, 2}, false, false).empty());
    CHECK_FALSE(mode.Start);
}

TEST_CASE("Esc first drops the wire in progress, then leaves wire mode", "[editor_mode]") {
    GUI::WiringMode mode{.Start = GUI::GridPoint{2, 2}};
    CHECK_FALSE(GUI::StopWire(mode));
    CHECK_FALSE(mode.Start);
    CHECK(GUI::StopWire(mode));
}

TEST_CASE("The status hint is that of the mode, or that of the selection while selecting", "[editor_mode]") {
    GUI::Schematic schematic;
    CHECK(GUI::DescribeEditorHint(GUI::ProbingMode{}, schematic, false).starts_with("Probe:"));
    CHECK(GUI::DescribeEditorHint(GUI::SelectingMode{}, schematic, false).starts_with("Click a part or wire"));
    CHECK(GUI::DescribeEditorHint(GUI::SelectingMode{}, schematic, true).starts_with("Live:"));

    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.SelectElement(0);
    // A part without a name is told by its kind, and a named one by its name
    CHECK(GUI::DescribeEditorHint(GUI::SelectingMode{}, schematic, false).starts_with("R"));
    schematic.GetElement(0).GetComponent().SetName("R7");
    CHECK(GUI::DescribeEditorHint(GUI::SelectingMode{}, schematic, false).starts_with("R7 selected:"));
}
