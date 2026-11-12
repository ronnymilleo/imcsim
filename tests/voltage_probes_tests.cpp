/**
 * @file    voltage_probes_tests.cpp
 * @brief   Tests for measured nodes that follow their net when the schematic is edited.
 */

#include "schematic/connectivity.h"
#include "schematic/element_factory.h"
#include "schematic/voltage_probes.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace {

/**
 * @struct  TwoNets
 * @brief   A resistor from (-2, 0) to (2, 0) with a wire from (2, 0) to (6, 0), and a ground at (0, 8) joined by a wire
 *          from (-4, 8) to it.
 */
struct TwoNets {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires;

    TwoNets() {
        Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
        Elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {0, 8}, GUI::Rotation::R0));
        Wires.emplace_back(GUI::GridPoint{2, 0}, GUI::GridPoint{6, 0});
        Wires.emplace_back(GUI::GridPoint{-4, 8}, GUI::GridPoint{0, 8});
    }
};

} // namespace

TEST_CASE("A probe follows its net to the node the net has now", "[voltage_probes]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const int node = *connectivity.GetNode({6, 0});
    // The probe was made on a node with another number and only one of its points is on the net still
    const GUI::VoltageProbe probe{.Points = {{6, 0}, {100, 100}}, .Node = 99};

    const std::vector<GUI::VoltageProbe> followed = GUI::FollowVoltageProbes({probe}, connectivity);

    REQUIRE(followed.size() == 1);
    CHECK(followed.front().Node == node);
    CHECK(followed.front().Points == connectivity.GetPointsOfNode(node));
}

TEST_CASE("A probe on ground waits with its points and has no node", "[voltage_probes]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const GUI::VoltageProbe probe{.Points = {{-4, 8}, {0, 8}}, .Node = 3};

    const std::vector<GUI::VoltageProbe> followed = GUI::FollowVoltageProbes({probe}, connectivity);

    REQUIRE(followed.size() == 1);
    CHECK_FALSE(followed.front().Node.has_value());
    CHECK(followed.front().Points == probe.Points);
}

TEST_CASE("Two probes that end up on one node are one", "[voltage_probes]") {
    const TwoNets schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const GUI::VoltageProbe first{.Points = {{6, 0}}, .Node = 1};
    const GUI::VoltageProbe second{.Points = {{2, 0}}, .Node = 2};

    const std::vector<GUI::VoltageProbe> followed = GUI::FollowVoltageProbes({first, second}, connectivity);

    CHECK(followed.size() == 1);
}
