/**
 * @file    connectivity_tests.cpp
 * @brief   Tests for grouping terminals and wires into circuit nodes.
 */

#include "connectivity.h"
#include "test_printers.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_resistor.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace {

/**
 * @struct  Schematic
 * @brief   Elements and wires to build a Connectivity from in a test.
 */
struct Schematic {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires;

    GUI::Connectivity Connect() const { return {Elements, Wires}; }
};

} // namespace

TEST_CASE("A wire puts both of its ends in the same node", "[connectivity]") {
    Schematic schematic;
    schematic.Wires.emplace_back(GUI::GridPoint{0, 0}, GUI::GridPoint{0, 4});
    schematic.Wires.emplace_back(GUI::GridPoint{0, 4}, GUI::GridPoint{6, 4});

    const GUI::Connectivity connectivity = schematic.Connect();
    REQUIRE(connectivity.GetNode({0, 0}).has_value());
    CHECK(connectivity.GetNode({0, 0}) == connectivity.GetNode({6, 4}));
}

TEST_CASE("The node touching a ground terminal is node 0", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    schematic.Elements.push_back(std::make_unique<GUI::UIGround>(GUI::GridPoint{2, 3}, GUI::Rotation::R0));
    schematic.Wires.emplace_back(GUI::GridPoint{2, 0}, GUI::GridPoint{2, 3});

    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({2, 0}) == 0);
    CHECK(connectivity.GetNode({-2, 0}) == 1);
}

TEST_CASE("A wire ending on the middle of another wire connects to it", "[connectivity]") {
    Schematic schematic;
    schematic.Wires.emplace_back(GUI::GridPoint{0, 0}, GUI::GridPoint{8, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{4, 0}, GUI::GridPoint{4, 5});

    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({4, 5}) == connectivity.GetNode({0, 0}));
    CHECK(connectivity.GetJunctions() == std::vector<GUI::GridPoint>{{4, 0}});
}

TEST_CASE("Wires that only cross do not connect", "[connectivity]") {
    Schematic schematic;
    schematic.Wires.emplace_back(GUI::GridPoint{-4, 0}, GUI::GridPoint{4, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{0, -4}, GUI::GridPoint{0, 4});

    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({-4, 0}) != connectivity.GetNode({0, -4}));
    CHECK(connectivity.GetJunctions().empty());
}

TEST_CASE("Terminals on the same grid point connect without a wire", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{4, 0}, GUI::Rotation::R0));

    // The first resistor ends at (2, 0), exactly where the second one starts
    const GUI::Connectivity connectivity = schematic.Connect();
    const Core::Circuit circuit = GUI::BuildCircuit(schematic.Elements, connectivity);
    REQUIRE(circuit.GetEntries().size() == 2);
    CHECK(circuit.GetEntries()[0].Nodes[1] == circuit.GetEntries()[1].Nodes[0]);
    CHECK(circuit.GetEntries()[0].Nodes[0] != circuit.GetEntries()[1].Nodes[1]);
}

TEST_CASE("Junction dots need three connections", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    // A wire from a terminal with a bend: two connections at the terminal and two at the bend
    schematic.Wires.emplace_back(GUI::GridPoint{2, 0}, GUI::GridPoint{5, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{5, 0}, GUI::GridPoint{5, 3});

    CHECK(schematic.Connect().GetJunctions().empty());

    schematic.Wires.emplace_back(GUI::GridPoint{5, 0}, GUI::GridPoint{8, 0});
    CHECK(schematic.Connect().GetJunctions() == std::vector<GUI::GridPoint>{{5, 0}});
}

TEST_CASE("Points with nothing connected have no node", "[connectivity]") {
    const Schematic schematic;
    CHECK_FALSE(schematic.Connect().GetNode({3, 3}).has_value());
}

TEST_CASE("BuildCircuit lists the node of every terminal in order", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    schematic.Elements.push_back(std::make_unique<GUI::UIGround>(GUI::GridPoint{2, 0}, GUI::Rotation::R0));

    const GUI::Connectivity connectivity = schematic.Connect();
    const Core::Circuit circuit = GUI::BuildCircuit(schematic.Elements, connectivity);
    REQUIRE(circuit.GetEntries().size() == 2);
    CHECK(circuit.GetEntries()[0].Nodes == std::vector<int>{1, 0});
    CHECK(circuit.GetEntries()[1].Nodes == std::vector<int>{0});
    CHECK(circuit.GetNodeCount() == 2);
}
