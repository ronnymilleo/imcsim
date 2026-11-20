/**
 * @file    hovered_value_tests.cpp
 * @brief   Tests for the text of the tooltip over a wire or a part.
 */

#include "overlays/hovered_value.h"

#include "schematic/element_factory.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

std::vector<std::unique_ptr<GUI::UIElement>> MakeParts() {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::FUSE, {10, 0}, GUI::Rotation::R0));
    elements[0]->GetComponent().SetName("R1");
    elements[1]->GetComponent().SetName("FU1");
    return elements;
}

} // namespace

TEST_CASE("The tooltip of a node reads its voltage", "[hovered_value]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeParts();
    const GUI::Connectivity connectivity(elements, {});
    GUI::DisplayedState displayed;
    displayed.Point.NodeVoltages = {0.0, 4.5};
    CHECK(GUI::DescribeHoveredValue({.Node = 1, .Current = {}}, connectivity, elements, displayed) == "V(1) = 4.5V");
    // The result has no voltage for node 2
    CHECK_FALSE(GUI::DescribeHoveredValue({.Node = 2, .Current = {}}, connectivity, elements, displayed).has_value());
}

TEST_CASE("The tooltip of a part reads its current and the power it absorbs", "[hovered_value]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeParts();
    const GUI::Connectivity connectivity(elements, {});
    GUI::DisplayedState displayed;
    displayed.Point.Currents = {{"R1", 2e-3}};
    displayed.Powers = {{.Name = "R1", .Power = 4e-3}};
    CHECK(GUI::DescribeHoveredValue({.Node = std::nullopt, .Current = "R1"}, connectivity, elements, displayed)
          == "I(R1) = 2mA\nP(R1) = 4mW");
}

TEST_CASE("The tooltip of a fuse tells when it blew, once the instant on display is past it", "[hovered_value]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> elements = MakeParts();
    const GUI::Connectivity connectivity(elements, {});
    GUI::DisplayedState displayed;
    displayed.Point.Currents = {{"FU1", 5e-9}};
    displayed.FuseBlowTimes = {std::nullopt, 0.2};
    displayed.Time = 0.25;
    const GUI::MeasurementTarget fuse = {.Node = std::nullopt, .Current = "FU1"};
    CHECK(GUI::DescribeHoveredValue(fuse, connectivity, elements, displayed) == "I(FU1) = 5nA\nFU1 blew at t = 200ms");
    displayed.Time = 0.1;
    CHECK(GUI::DescribeHoveredValue(fuse, connectivity, elements, displayed) == "I(FU1) = 5nA");
}
