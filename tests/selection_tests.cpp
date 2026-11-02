/**
 * @file    selection_tests.cpp
 * @brief   Tests for picking elements and wires with a click, a selection box and a node.
 */

#include "connectivity.h"
#include "element_factory.h"
#include "selection.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace {

std::vector<std::unique_ptr<GUI::UIElement>> TwoResistors() {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {12, 0}, GUI::Rotation::R0));
    return elements;
}

} // namespace

TEST_CASE("A click picks an element before a wire, and the newest of two on top of each other", "[selection]") {
    auto elements = TwoResistors();
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    const std::vector<GUI::UIWire> wires = {{{-2, 0}, {-2, 6}}};

    CHECK(GUI::PickAt(elements, wires, {0.0f, 0.0f}, 0.2f) == GUI::Selection{.Elements = {2}, .Wires = {}});
    CHECK(GUI::PickAt(elements, wires, {12.0f, 0.0f}, 0.2f) == GUI::Selection{.Elements = {1}, .Wires = {}});
    // At the end of the part, a wire there is picked only where the part is not: (-2, 3) is below it
    CHECK(GUI::PickAt(elements, wires, {-2.0f, 3.0f}, 0.2f) == GUI::Selection{.Elements = {}, .Wires = {0}});
    CHECK(GUI::PickAt(elements, wires, {6.0f, 6.0f}, 0.2f).Empty());
}

TEST_CASE("A selection box picks what it touches, whichever way it is dragged", "[selection]") {
    const auto elements = TwoResistors();
    const std::vector<GUI::UIWire> wires = {{{2, 0}, {10, 0}}, {{30, 0}, {30, 4}}};

    // Touches the first part and the first wire, not the second part
    const GUI::Selection left = GUI::PickInRectangle(elements, wires, {-1.0f, -1.0f}, {5.0f, 1.0f});
    CHECK(left == GUI::Selection{.Elements = {0}, .Wires = {0}});
    // The same box dragged from the opposite corner
    CHECK(GUI::PickInRectangle(elements, wires, {5.0f, 1.0f}, {-1.0f, -1.0f}) == left);
    // The edge of a wire counts as touching, and an empty area picks nothing
    CHECK(GUI::PickInRectangle(elements, wires, {10.0f, -3.0f}, {11.0f, 0.0f}).Wires == std::set<std::size_t>{0});
    CHECK(GUI::PickInRectangle(elements, wires, {20.0f, 20.0f}, {22.0f, 22.0f}).Empty());
}

TEST_CASE("A selection box finds a part turned a quarter", "[selection]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R90));
    // Turned, the part runs from y = -2 to 2 and is narrow in x
    CHECK(GUI::PickInRectangle(elements, {}, {1.5f, -0.5f}, {3.0f, 0.5f}).Empty());
    CHECK(GUI::PickInRectangle(elements, {}, {-0.5f, 1.5f}, {0.5f, 3.0f}).Elements == std::set<std::size_t>{0});
    CHECK(GUI::PickInRectangle(elements, {}, {1.5f, 1.5f}, {3.0f, 3.0f}).Empty());
}

TEST_CASE("Picking a node takes every wire of the node and no other", "[selection]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements;
    // Three wires meet at (4, 0); the fourth is another node
    const std::vector<GUI::UIWire> wires = {{{0, 0}, {4, 0}}, {{4, 0}, {4, 4}}, {{4, 0}, {8, 0}}, {{0, 10}, {4, 10}}};
    const GUI::Connectivity connectivity(elements, wires);

    CHECK(GUI::PickNode(wires, connectivity, 1) == GUI::Selection{.Elements = {}, .Wires = {0, 1, 2}});
    CHECK(GUI::PickNode(wires, connectivity, 3) == GUI::Selection{.Elements = {}, .Wires = {3}});
    CHECK(GUI::PickNode(wires, connectivity, 9).Empty());
}

TEST_CASE("Toggling flips picked items and joining adds them", "[selection]") {
    const GUI::Selection current = {.Elements = {0, 1}, .Wires = {2}};
    const GUI::Selection picked = {.Elements = {1, 3}, .Wires = {2, 4}};

    CHECK(GUI::Toggled(current, picked) == GUI::Selection{.Elements = {0, 3}, .Wires = {4}});
    CHECK(GUI::Joined(current, picked) == GUI::Selection{.Elements = {0, 1, 3}, .Wires = {2, 4}});
}

TEST_CASE("PickAll takes every element and wire", "[selection]") {
    const auto elements = TwoResistors();
    const std::vector<GUI::UIWire> wires = {{{2, 0}, {10, 0}}};
    CHECK(GUI::PickAll(elements, wires) == GUI::Selection{.Elements = {0, 1}, .Wires = {0}});
}
