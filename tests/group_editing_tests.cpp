/**
 * @file    group_editing_tests.cpp
 * @brief   Tests for turning and mirroring parts about a point and finding that point.
 */

#include "element_factory.h"
#include "group_editing.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace {

GUI::PointTransform Transform(const GUI::PointTransform::Kind kind) {
    return {.Type = kind, .Delta = {3, -2}, .Pivot = {5, 1}};
}

} // namespace

TEST_CASE("A part ends up where the transform puts each of its terminals", "[group_editing]") {
    using Kind = GUI::PointTransform::Kind;
    // A transistor has terminals that are not symmetric, so every orientation and flip of it is told apart
    for (const Kind kind : {Kind::TRANSLATE, Kind::QUARTER_TURN, Kind::MIRROR_LEFT_RIGHT, Kind::MIRROR_TOP_BOTTOM}) {
        for (const GUI::Rotation rotation :
             {GUI::Rotation::R0, GUI::Rotation::R90, GUI::Rotation::R180, GUI::Rotation::R270}) {
            for (const bool mirrored : {false, true}) {
                const auto element = GUI::CreateElement(Core::ComponentType::NPN, {8, 3}, rotation);
                element->SetMirrored(mirrored);
                const std::vector<GUI::GridPoint> before = element->GetTerminals();
                const GUI::PointTransform transform = Transform(kind);

                GUI::TransformElement(*element, transform);

                const std::vector<GUI::GridPoint> after = element->GetTerminals();
                REQUIRE(after.size() == before.size());
                for (std::size_t index = 0; index < before.size(); ++index) {
                    CHECK(after[index] == transform.Apply(before[index]));
                }
            }
        }
    }
}

TEST_CASE("A single part turns about its own position, a group about the middle of its box", "[group_editing]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {2, 2}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 6}, GUI::Rotation::R0));
    const std::vector<GUI::UIWire> wires = {{{12, 6}, {12, 12}}};

    CHECK(GUI::FindPivot(elements, {1}, wires, {}) == GUI::GridPoint{10, 6});
    CHECK(GUI::FindPivot(elements, {0, 1}, wires, {}) == GUI::GridPoint{6, 4});
    // A wire counts by its ends, and a lone wire is no single part
    CHECK(GUI::FindPivot(elements, {1}, wires, {0}) == GUI::GridPoint{11, 9});
    CHECK(GUI::FindPivot(elements, {}, wires, {}) == GUI::GridPoint{0, 0});
}
