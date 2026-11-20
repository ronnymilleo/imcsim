/**
 * @file    node_readouts_tests.cpp
 * @brief   Tests for which nodes get a voltage readout and what each one sits beside.
 */

#include "overlays/node_readouts.h"

#include "schematic/connectivity.h"
#include "schematic/element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace {

/**
 * @brief   Two resistors in series from a wire to ground: R1 from (-2, 0) to (2, 0) and R2 from (2, 0) to (6, 0),
 *          joined at their shared terminal without a wire, with ground at (6, 0).
 */
std::vector<std::unique_ptr<GUI::UIElement>> MakeDivider() {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {4, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {6, 0}, GUI::Rotation::R0));
    return elements;
}

} // namespace

TEST_CASE("Readouts are tried beside the wires first, then beside the terminals", "[node_readouts]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeDivider();
    const std::vector<GUI::UIWire> wires = {{{-6, 0}, {-2, 0}}};
    const std::vector<GUI::ReadoutAnchor> anchors = GUI::ListReadoutAnchors(wires, elements);
    REQUIRE(anchors.size() == 6);
    CHECK(anchors[0].Point == GUI::GridPoint{-6, 0});
    CHECK(anchors[0].Start.x == -6.0f);
    CHECK(anchors[0].End.x == -2.0f);
    // A terminal is a point, so a readout beside it has the same two ends
    CHECK(anchors[1].Point == GUI::GridPoint{-2, 0});
    CHECK(anchors[2].Point == GUI::GridPoint{2, 0});
    CHECK(anchors[2].Start.x == anchors[2].End.x);
    CHECK(anchors[5].Point == GUI::GridPoint{6, 0});
}

TEST_CASE("Each node above ground gets one readout, at its first anchor", "[node_readouts]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeDivider();
    const std::vector<GUI::UIWire> wires = {{{-6, 0}, {-2, 0}}};
    const GUI::Connectivity connectivity(elements, wires);
    const std::optional<int> input = connectivity.GetNode({-6, 0});
    const std::optional<int> middle = connectivity.GetNode({2, 0});
    REQUIRE(input.has_value());
    REQUIRE(middle.has_value());
    REQUIRE(connectivity.GetNode({6, 0}) == 0);

    const std::vector<GUI::NodeReadout> readouts =
        GUI::ChooseNodeReadouts(GUI::ListReadoutAnchors(wires, elements), connectivity, 3);
    REQUIRE(readouts.size() == 2);
    // The input node reads beside its wire, not beside the terminal on the same node
    CHECK(readouts[0].Node == static_cast<std::size_t>(*input));
    CHECK(readouts[0].Anchor.Point == GUI::GridPoint{-6, 0});
    // The middle node has no wire, so it reads beside the first terminal on it
    CHECK(readouts[1].Node == static_cast<std::size_t>(*middle));
    CHECK(readouts[1].Anchor.Point == GUI::GridPoint{2, 0});
}

TEST_CASE("A node the result has no voltage for gets no readout", "[node_readouts]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeDivider();
    const GUI::Connectivity connectivity(elements, {});
    CHECK(GUI::ChooseNodeReadouts(GUI::ListReadoutAnchors({}, elements), connectivity, 1).empty());
}
