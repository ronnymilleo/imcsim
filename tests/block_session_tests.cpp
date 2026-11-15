/**
 * @file    block_session_tests.cpp
 * @brief   Tests for the blocks the editor has open: where a block may be placed, and what opening and closing one
 *          does to the document.
 */

#include "circuit/components/net_label.h"
#include "schematic/block_session.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

/**
 * @brief   A pin marker; unturned, its arrow points right, which puts the pin on the left of the block.
 */
void AddMarker(GUI::Schematic &schematic, const GUI::GridPoint point, const std::string &name,
               const GUI::Rotation rotation = GUI::Rotation::R0) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::PIN, point, rotation));
    static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet(name);
}

void SelectEverything(GUI::Schematic &schematic) {
    std::set<std::size_t> elements;
    for (std::size_t index = 0; index < schematic.GetElements().size(); ++index) {
        elements.insert(index);
    }
    std::set<std::size_t> wires;
    for (std::size_t index = 0; index < schematic.GetWires().size(); ++index) {
        wires.insert(index);
    }
    schematic.SetSelection(std::move(elements), std::move(wires));
}

/**
 * @brief   A resistor between IN and OUT markers turned into the block "inner", then that block with markers on its
 *          wires turned into the block "outer"; the schematic ends with one outer instance.
 */
void MakeNestedBlocks(GUI::Schematic &schematic) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    SelectEverything(schematic);
    REQUIRE(schematic.CreateSubcircuitFromSelection("inner", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    AddMarker(schematic, {-2, 0}, "A");
    AddMarker(schematic, {2, 0}, "B", GUI::Rotation::R180);
    SelectEverything(schematic);
    REQUIRE(schematic.CreateSubcircuitFromSelection("outer", {{"A", {-2, 0}}, {"B", {2, 0}}}));
    schematic.CommitUndoStep();
}

const GUI::SavedSubcircuit &FindDefinition(const GUI::Schematic &schematic, const std::string &name) {
    const auto found = std::ranges::find(schematic.GetSubcircuits(), name, &GUI::SavedSubcircuit::Name);
    REQUIRE(found != schematic.GetSubcircuits().end());
    return *found;
}

} // namespace

TEST_CASE("A block cannot be placed inside itself or inside a block it contains", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    GUI::BlockSession session(schematic);
    // On the document every block can be placed
    CHECK(session.CanPlaceBlock("inner"));
    CHECK(session.CanPlaceBlock("outer"));
    CHECK(session.ListPlaceableBlocks().size() == 2);

    REQUIRE(session.Open("inner"));
    CHECK_FALSE(session.CanPlaceBlock("inner"));
    // Outer places inner, so placing it inside inner would make a cycle
    CHECK_FALSE(session.CanPlaceBlock("outer"));
    CHECK(session.ListPlaceableBlocks().empty());
    session.Close(false);

    REQUIRE(session.Open("outer"));
    CHECK_FALSE(session.CanPlaceBlock("outer"));
    CHECK(session.CanPlaceBlock("inner"));
    REQUIRE(session.ListPlaceableBlocks().size() == 1);
    CHECK(session.ListPlaceableBlocks().front()->Name == "inner");
}

TEST_CASE("Opening a block shows its contents with its pins, and closing it goes back one level", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    GUI::BlockSession session(schematic);
    CHECK_FALSE(session.IsOpen());
    CHECK(&session.GetShown() == &schematic);

    REQUIRE(session.Open("outer"));
    CHECK(session.IsOpen());
    CHECK(session.GetName() == "outer");
    CHECK(session.GetPins().size() == 2);
    CHECK(&session.GetShown() != &schematic);

    // The way back up keeps the settings of the level that was left
    session.GetPins()[0].Name = "CHANGED";
    REQUIRE(session.Open("inner"));
    CHECK(session.GetOpenBlocks().size() == 2);
    CHECK(session.GetName() == "inner");
    CHECK(session.Close(false));
    CHECK(session.GetName() == "outer");
    CHECK(session.GetPins()[0].Name == "CHANGED");
    CHECK(session.Close(false));
    CHECK_FALSE(session.IsOpen());
    CHECK(&session.GetShown() == &schematic);
    CHECK_FALSE(session.Close(false));
}

TEST_CASE("A block opened and closed without a change leaves the document unmodified", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    schematic.MarkSaved({});
    GUI::BlockSession session(schematic);
    REQUIRE(session.Open("inner"));
    REQUIRE(session.Close(true));
    CHECK_FALSE(schematic.IsModified());
}

TEST_CASE("Applying a block puts its new contents in the document as one undo step, dropping it puts none",
          "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    schematic.MarkSaved({});
    GUI::BlockSession session(schematic);
    const std::size_t parts = FindDefinition(schematic, "inner").Elements.size();

    REQUIRE(session.Open("inner"));
    session.GetShown().AddElement(GUI::CreateElement(Core::ComponentType::CAPACITOR, {0, 6}, GUI::Rotation::R0));
    REQUIRE(session.Close(false));
    CHECK(FindDefinition(schematic, "inner").Elements.size() == parts);
    CHECK_FALSE(schematic.IsModified());

    REQUIRE(session.Open("inner"));
    session.GetShown().AddElement(GUI::CreateElement(Core::ComponentType::CAPACITOR, {0, 6}, GUI::Rotation::R0));
    REQUIRE(session.Close(true));
    CHECK(FindDefinition(schematic, "inner").Elements.size() == parts + 1);
    CHECK(schematic.IsModified());
    CHECK(schematic.CanUndo());
}

TEST_CASE("The blocks open take the definitions the document gained", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    GUI::BlockSession session(schematic);
    REQUIRE(session.Open("inner"));
    REQUIRE(session.GetShown().GetSubcircuits().size() == 1);

    // A paste into the block brings a block definition to the document first
    std::vector<GUI::SavedSubcircuit> brought(1);
    brought.front().Name = "brought";
    std::vector<std::unique_ptr<GUI::UIElement>> no_parts;
    schematic.MergeSubcircuits(std::move(brought), no_parts);
    session.SyncDefinitions();
    CHECK(session.GetShown().GetSubcircuits().size() == 2);
}

TEST_CASE("The pin markers of a block take the names of its pins, and every marker needs a pin", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    GUI::BlockSession session(schematic);
    REQUIRE(session.Open("inner"));
    session.GetPins()[0].Name = "SIGNAL";
    REQUIRE(session.ApplyPinNames());
    const auto &marker = static_cast<const Core::NetLabel &>(session.GetShown().GetElement(1).GetComponent());
    CHECK(marker.GetNet() == "SIGNAL");

    session.GetPins().clear();
    CHECK_FALSE(session.ApplyPinNames());
    CHECK(session.GetError() == "Every pin marker needs a block pin name");
}

TEST_CASE("Pins follow the markers of the open block: the order stays and new markers join at the end",
          "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    GUI::BlockSession session(schematic);
    REQUIRE(session.Open("inner"));
    std::swap(session.GetPins()[0], session.GetPins()[1]);
    AddMarker(session.GetShown(), {0, 4}, "EXTRA", GUI::Rotation::R90);
    REQUIRE(session.SyncPins());
    REQUIRE(session.GetPins().size() == 3);
    CHECK(session.GetPins()[0].Name == "OUT");
    CHECK(session.GetPins()[1].Name == "IN");
    CHECK(session.GetPins()[2].Name == "EXTRA");
}

TEST_CASE("The name of the block an element places is none for any other element", "[block_session]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    CHECK(GUI::BlockSession::FindBlockName(schematic.GetElements().front().get()) == "outer");
    const auto resistor = GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0);
    CHECK_FALSE(GUI::BlockSession::FindBlockName(resistor.get()));
    CHECK_FALSE(GUI::BlockSession::FindBlockName(nullptr));
}

TEST_CASE("A new block takes a free name and offers the pins of the selection", "[block_session]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    SelectEverything(schematic);
    GUI::BlockSession session(schematic);
    session.RequestCreate();
    CHECK(session.GetName() == "block");
    CHECK(session.GetPins().size() == 2);
    CHECK(session.TakeDialogRequest());
    CHECK_FALSE(session.TakeDialogRequest());

    REQUIRE(session.CreateBlock());
    CHECK(schematic.GetSubcircuits().size() == 1);
    session.RequestCreate();
    CHECK(session.GetName() == "block_2");
}
