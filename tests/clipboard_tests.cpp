/**
 * @file    clipboard_tests.cpp
 * @brief   Tests for copying parts and wires to text and reading them back for a paste.
 */

#include "clipboard.h"
#include "element_factory.h"
#include "schematic.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace {

std::vector<std::unique_ptr<GUI::UIElement>> Parts() {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {10, 10}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::Capacitor, {20, 10}, GUI::Rotation::R90));
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {40, 30}, GUI::Rotation::R0));
    elements[0]->GetComponent().SetName("R1");
    elements[1]->GetComponent().SetName("C1");
    elements[2]->GetComponent().SetName("R2");
    elements[0]->GetComponent().SetValue(4700.0);
    return elements;
}

const std::vector<GUI::UIWire> Wires = {{{12, 10}, {18, 10}}, {{22, 10}, {30, 10}}, {{40, 28}, {40, 20}}};

} // namespace

TEST_CASE("Copying keeps only the selected parts and wires and pastes back as they were", "[clipboard]") {
    const auto elements = Parts();
    const std::string text = GUI::CopySelection(elements, Wires, {.Elements = {0, 1}, .Wires = {0}});

    REQUIRE(GUI::LooksLikeCopiedSchematic(text));
    auto group = GUI::ParseCopiedSchematic(text);
    REQUIRE(group);
    REQUIRE(group->Elements.size() == 2);
    CHECK(group->Elements[0]->GetComponent().GetType() == Core::ComponentType::Resistor);
    CHECK(group->Elements[0]->GetComponent().GetValue() == 4700.0);
    CHECK(group->Elements[0]->GetPosition() == GUI::GridPoint{10, 10});
    CHECK(group->Elements[1]->GetRotation() == GUI::Rotation::R90);
    REQUIRE(group->Wires.size() == 1);
    CHECK(group->Wires[0] == GUI::UIWire({12, 10}, {18, 10}));
}

TEST_CASE("A group is held by the middle of the box around its parts and wire ends", "[clipboard]") {
    const auto elements = Parts();
    auto group = GUI::ParseCopiedSchematic(GUI::CopySelection(elements, Wires, {.Elements = {0, 1}, .Wires = {0, 1}}));
    REQUIRE(group);
    // From x = 10, the first part, to x = 30, the end of the last wire
    CHECK(group->Pivot == GUI::GridPoint{20, 10});
}

TEST_CASE("Moving a group by its pivot moves its parts and wires together", "[clipboard]") {
    const auto elements = Parts();
    auto group = GUI::ParseCopiedSchematic(GUI::CopySelection(elements, Wires, {.Elements = {0, 1}, .Wires = {0}}));
    REQUIRE(group);

    // The group is held at (15, 10), halfway between its parts, so it moves by (11, 4)
    REQUIRE(group->Pivot == GUI::GridPoint{15, 10});
    GUI::MovePivotTo(*group, {26, 14});
    CHECK(group->Pivot == GUI::GridPoint{26, 14});
    CHECK(group->Elements[0]->GetPosition() == GUI::GridPoint{21, 14});
    CHECK(group->Elements[1]->GetPosition() == GUI::GridPoint{31, 14});
    CHECK(group->Wires[0] == GUI::UIWire({23, 14}, {29, 14}));
}

TEST_CASE("Turning a group about its pivot turns its parts and wires", "[clipboard]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {10, 10}, GUI::Rotation::R0));
    const std::vector<GUI::UIWire> wires = {{{12, 10}, {18, 10}}};
    auto group = GUI::ParseCopiedSchematic(GUI::CopySelection(elements, wires, {.Elements = {0}, .Wires = {0}}));
    REQUIRE(group);
    // The box runs from x = 10, the part, to 18, the end of the wire, so the pivot is (14, 10)
    REQUIRE(group->Pivot == GUI::GridPoint{14, 10});

    GUI::TransformGroup(*group, {.Type = GUI::PointTransform::Kind::QuarterTurn, .Delta = {}, .Pivot = {}});
    CHECK(group->Pivot == GUI::GridPoint{14, 10});
    CHECK(group->Elements[0]->GetPosition() == GUI::GridPoint{14, 6});
    CHECK(group->Elements[0]->GetRotation() == GUI::Rotation::R90);
    // A quarter turn takes what is right of the pivot to above it
    CHECK(group->Wires[0] == GUI::UIWire({14, 8}, {14, 14}));
}

TEST_CASE("Text that is not a schematic, or has nothing in it, cannot be pasted", "[clipboard]") {
    CHECK_FALSE(GUI::LooksLikeCopiedSchematic("R1 1 0 1k"));
    CHECK_FALSE(GUI::ParseCopiedSchematic("R1 1 0 1k"));
    CHECK_FALSE(GUI::ParseCopiedSchematic("{\"hello\": 1}"));
    const auto elements = Parts();
    CHECK_FALSE(GUI::ParseCopiedSchematic(GUI::CopySelection(elements, Wires, {})));
}

TEST_CASE("Pasted parts get the next free names of the schematic", "[clipboard]") {
    GUI::Schematic schematic;
    for (auto &element : Parts()) {
        schematic.AddElement(std::move(element));
    }
    auto group = GUI::ParseCopiedSchematic(GUI::CopySelection(schematic.GetElements(), Wires, {.Elements = {0, 1}}));
    REQUIRE(group);

    for (auto &element : group->Elements) {
        schematic.AddElement(std::move(element));
    }
    REQUIRE(schematic.GetElements().size() == 5);
    CHECK(schematic.GetElements()[3]->GetComponent().GetName() == "R3");
    CHECK(schematic.GetElements()[4]->GetComponent().GetName() == "C2");
}
