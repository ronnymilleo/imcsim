/**
 * @file    selection_editing_tests.cpp
 * @brief   Tests for what the editor does to the selection: replacing it, dragging it, turning it and clearing it.
 */

#include "schematic/editor_mode.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "schematic/selection_editing.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <vector>

namespace {

/**
 * @brief   A resistor at the origin, which spans (-2, 0) to (2, 0), with a wire that leaves it from its right end.
 */
GUI::Schematic ResistorWithWire() {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddWire({2, 0}, {6, 0});
    return schematic;
}

} // namespace

TEST_CASE("A drag is in progress only while the selecting mode holds one", "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    schematic.SetSelection({0}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    CHECK_FALSE(GUI::IsDragging(mode));
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {0, 0}, std::nullopt);
    CHECK(GUI::IsDragging(mode));
    CHECK_FALSE(GUI::IsDragging(GUI::WiringMode{}));
}

TEST_CASE("A drag moves the selected parts by how far the cursor went, and the wires on them follow",
          "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    schematic.SetSelection({0}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {0, 0}, std::nullopt);

    GUI::UpdateDrag(schematic, mode, {4, 2});
    CHECK(schematic.GetElement(0).GetPosition() == GUI::GridPoint{4, 2});
    // The wire stays on the right end of the part
    CHECK(schematic.GetWires().front().GetStart() == GUI::GridPoint{6, 2});

    // Moving back goes from the start of the drag, not from the last frame
    GUI::UpdateDrag(schematic, mode, {0, 0});
    CHECK(schematic.GetElement(0).GetPosition() == GUI::GridPoint{0, 0});
}

TEST_CASE("A drag that never moved a group leaves just the item that was clicked", "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {12, 0}, GUI::Rotation::R0));
    schematic.SetSelection({0, 1}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    const GUI::Selection clicked = {.Elements = {1}, .Wires = {}};
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {12, 0}, clicked);

    GUI::EndDrag(schematic, mode);
    CHECK_FALSE(GUI::IsDragging(mode));
    CHECK(schematic.GetSelectedElements() == std::set<std::size_t>{1});

    // The same drag that moved first keeps the group
    schematic.SetSelection({0, 1}, {});
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {12, 0}, clicked);
    GUI::UpdateDrag(schematic, mode, {13, 0});
    GUI::EndDrag(schematic, mode);
    CHECK(schematic.GetSelectedElements() == std::set<std::size_t>{0, 1});
}

TEST_CASE("Replacing or clearing the selection ends the drag, and clearing drops the selection box",
          "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    schematic.SetSelection({0}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    std::get<GUI::SelectingMode>(mode).Drag = GUI::MakeDrag(schematic, {0, 0}, std::nullopt);

    GUI::ApplySelection(schematic, mode, {.Elements = {}, .Wires = {0}});
    CHECK_FALSE(GUI::IsDragging(mode));
    CHECK(schematic.GetSelectedElements().empty());
    CHECK(schematic.GetSelectedWires() == std::set<std::size_t>{0});

    std::get<GUI::SelectingMode>(mode).Box = GUI::SelectionBox{.Start = {0.0f, 0.0f}, .Base = {}};
    GUI::ClearSelection(schematic, mode);
    CHECK_FALSE(std::get<GUI::SelectingMode>(mode).Box);
    CHECK_FALSE(schematic.HasSelection());
}

TEST_CASE("Select all works only while selecting", "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    GUI::EditorMode mode = GUI::WiringMode{};
    GUI::SelectAll(schematic, mode);
    CHECK_FALSE(schematic.HasSelection());

    mode = GUI::SelectingMode{};
    GUI::SelectAll(schematic, mode);
    CHECK(schematic.GetSelectedElements() == std::set<std::size_t>{0});
    CHECK(schematic.GetSelectedWires() == std::set<std::size_t>{0});
}

TEST_CASE("Turning the selection takes the wires along unless they are asked to stay", "[selection_editing]") {
    GUI::Schematic following = ResistorWithWire();
    following.SetSelection({0}, {});
    GUI::EditorMode mode = GUI::SelectingMode{};
    GUI::TransformSelection(following, mode, GUI::PointTransform::Kind::QUARTER_TURN, false);
    CHECK(following.GetElement(0).GetRotation() != GUI::Rotation::R0);
    // The wires are still on a terminal of the part, wherever it went
    const std::vector<GUI::GridPoint> terminals = following.GetElement(0).GetTerminals();
    CHECK(following.GetWires().front().GetStart() != GUI::GridPoint{2, 0});
    CHECK(std::ranges::any_of(following.GetWires(), [&](const GUI::UIWire &wire) {
        return std::ranges::contains(terminals, wire.GetStart()) || std::ranges::contains(terminals, wire.GetEnd());
    }));

    GUI::Schematic staying = ResistorWithWire();
    staying.SetSelection({0}, {});
    GUI::TransformSelection(staying, mode, GUI::PointTransform::Kind::QUARTER_TURN, true);
    CHECK(staying.GetElement(0).GetRotation() != GUI::Rotation::R0);
    CHECK(staying.GetWires().front().GetStart() == GUI::GridPoint{2, 0});
    CHECK(staying.GetWires().front().GetEnd() == GUI::GridPoint{6, 0});
}

TEST_CASE("Turning with nothing selected changes nothing", "[selection_editing]") {
    GUI::Schematic schematic = ResistorWithWire();
    schematic.MarkSaved({});
    GUI::EditorMode mode = GUI::SelectingMode{};
    GUI::TransformSelection(schematic, mode, GUI::PointTransform::Kind::QUARTER_TURN, false);
    CHECK_FALSE(schematic.IsModified());
    CHECK(schematic.GetElement(0).GetRotation() == GUI::Rotation::R0);
}
