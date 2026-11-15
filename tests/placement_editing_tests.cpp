/**
 * @file    placement_editing_tests.cpp
 * @brief   Tests for changing the mode of the editor and for placing what a mode holds in the schematic.
 */

#include "circuit/components/net_label.h"
#include "circuit/components/subcircuit.h"
#include "schematic/block_session.h"
#include "schematic/clipboard.h"
#include "schematic/element_factory.h"
#include "schematic/placement_editing.h"
#include "schematic/schematic.h"
#include "schematic/selection_editing.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace {

void AddMarker(GUI::Schematic &schematic, const GUI::GridPoint point, const std::string &name,
               const GUI::Rotation rotation = GUI::Rotation::R0) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::PIN, point, rotation));
    static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet(name);
}

/**
 * @brief   A resistor between IN and OUT markers turned into the block "inner".
 */
void MakeBlock(GUI::Schematic &schematic) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.SetSelection({0, 1, 2}, {});
    REQUIRE(schematic.CreateSubcircuitFromSelection("inner", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    schematic.CommitUndoStep();
}

std::unique_ptr<GUI::UIElement> MakeInstance(const std::string &block) {
    auto element = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0);
    static_cast<Core::SubcircuitInstance &>(element->GetComponent()).SetDefinitionName(block);
    return element;
}

} // namespace

TEST_CASE("Starting a mode other than selecting empties the selection, and selecting keeps it", "[placement_editing]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.SetSelection({0}, {});
    GUI::BlockSession blocks(schematic);
    GUI::EditorMode mode = GUI::PastingMode{};

    // A paste ends in selecting with the new parts selected, which must stay so
    GUI::SwitchMode(mode, GUI::SelectingMode{}, schematic, blocks);
    CHECK(GUI::IsSelecting(mode));
    CHECK(schematic.HasSelection());

    GUI::SwitchMode(mode, GUI::WiringMode{}, schematic, blocks);
    CHECK(std::holds_alternative<GUI::WiringMode>(mode));
    CHECK_FALSE(schematic.HasSelection());
}

TEST_CASE("Asking for the wire mode again keeps the wire in progress", "[placement_editing]") {
    GUI::Schematic schematic;
    GUI::BlockSession blocks(schematic);
    GUI::EditorMode mode = GUI::WiringMode{.Start = GUI::GridPoint{3, 4}};
    GUI::SwitchMode(mode, GUI::WiringMode{}, schematic, blocks);
    CHECK(std::get<GUI::WiringMode>(mode).Start == GUI::GridPoint{3, 4});
}

TEST_CASE("A block is not placed where it would contain itself", "[placement_editing]") {
    GUI::Schematic schematic;
    MakeBlock(schematic);
    GUI::BlockSession blocks(schematic);
    GUI::EditorMode mode = GUI::SelectingMode{};
    GUI::SwitchMode(mode, GUI::PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = "inner"}, schematic,
                    blocks);
    CHECK(GUI::GetPlacingType(mode) == Core::ComponentType::SUBCIRCUIT);

    mode = GUI::SelectingMode{};
    REQUIRE(blocks.Open("inner"));
    GUI::SwitchMode(mode, GUI::PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = "inner"},
                    blocks.GetShown(), blocks);
    CHECK(GUI::IsSelecting(mode));
}

TEST_CASE("Stopping the editing ends a drag and a paste, and leaves probing alone", "[placement_editing]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.SetSelection({0}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {0, 0}, std::nullopt);
    GUI::StopEditing(schematic, mode);
    CHECK_FALSE(GUI::IsDragging(mode));

    mode = GUI::PastingMode{};
    GUI::StopEditing(schematic, mode);
    CHECK(GUI::IsSelecting(mode));

    mode = GUI::ProbingMode{};
    GUI::StopEditing(schematic, mode);
    CHECK(std::holds_alternative<GUI::ProbingMode>(mode));
}

TEST_CASE("A new label starts with the name of the newest label that has one", "[placement_editing]") {
    GUI::Schematic schematic;
    CHECK(GUI::FindLastNet(schematic).empty());
    for (const std::string &net : {"FIRST", "SECOND", ""}) {
        schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, {0, 0}, GUI::Rotation::R0));
        static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet(net);
    }
    CHECK(GUI::FindLastNet(schematic) == "SECOND");

    const auto preview = GUI::MakePlacementPreview(
        GUI::PlacingMode{.Type = Core::ComponentType::NET_LABEL, .IsMirrored = true}, {2, 2}, schematic, {});
    REQUIRE(preview);
    CHECK(static_cast<const Core::NetLabel &>(preview->GetComponent()).GetNet() == "SECOND");
    CHECK(preview->GetPosition() == GUI::GridPoint{2, 2});
    CHECK(preview->IsMirrored());
}

TEST_CASE("The preview of a block takes its pins, and none is made once the block is gone", "[placement_editing]") {
    GUI::Schematic schematic;
    MakeBlock(schematic);
    const GUI::PlacingMode placing = {.Type = Core::ComponentType::SUBCIRCUIT, .Block = "inner"};
    const auto preview = GUI::MakePlacementPreview(placing, {0, 0}, schematic, schematic.GetSubcircuits());
    REQUIRE(preview);
    CHECK(static_cast<const Core::SubcircuitInstance &>(preview->GetComponent()).GetPins() ==
          std::vector<std::string>{"IN", "OUT"});
    CHECK_FALSE(GUI::MakePlacementPreview(placing, {0, 0}, schematic, {}));
}

TEST_CASE("A pasted group joins the schematic and is what ends up selected", "[placement_editing]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    GUI::BlockSession blocks(schematic);
    GUI::PastedGroup group;
    group.Elements.push_back(GUI::CreateElement(Core::ComponentType::CAPACITOR, {10, 0}, GUI::Rotation::R0));
    group.Wires.emplace_back(GUI::GridPoint{12, 0}, GUI::GridPoint{16, 0});

    CHECK_FALSE(GUI::PlacePastedGroup(schematic, blocks, group));
    CHECK(schematic.GetElements().size() == 2);
    CHECK(schematic.GetSelectedElements() == std::set<std::size_t>{1});
    CHECK(schematic.GetSelectedWires() == std::set<std::size_t>{0});
}

TEST_CASE("A group that places the block being edited is refused inside that block", "[placement_editing]") {
    GUI::Schematic schematic;
    MakeBlock(schematic);
    GUI::BlockSession blocks(schematic);
    REQUIRE(blocks.Open("inner"));
    const std::size_t parts = blocks.GetShown().GetElements().size();

    GUI::PastedGroup group;
    group.Elements.push_back(MakeInstance("inner"));
    const std::optional<std::string> refusal = GUI::PlacePastedGroup(schematic, blocks, group);
    REQUIRE(refusal);
    CHECK(refusal->starts_with("inner would contain itself"));
    CHECK(blocks.GetShown().GetElements().size() == parts);

    // On the document the same group is fine
    blocks.Close(false);
    GUI::PastedGroup again;
    again.Elements.push_back(MakeInstance("inner"));
    CHECK_FALSE(GUI::PlacePastedGroup(schematic, blocks, again));
}
