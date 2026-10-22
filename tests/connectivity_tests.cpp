/**
 * @file    connectivity_tests.cpp
 * @brief   Tests for grouping terminals and wires into circuit nodes.
 */

#include "components/net_label.h"
#include "connectivity.h"
#include "test_printers.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_logic_input.h"
#include "ui_elements/ui_net_label.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_vcc.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
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

    // A label at a point; its terminal is at that point
    void Label(const GUI::GridPoint point, const std::string &net) {
        Elements.push_back(std::make_unique<GUI::UINetLabel>(point, GUI::Rotation::R0));
        static_cast<Core::NetLabel &>(Elements.back()->GetComponent()).SetNet(net);
    }
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

TEST_CASE("Without a ground part no node is numbered 0, so a digital circuit can run without one", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UILogicInput>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));

    const GUI::Connectivity connectivity = schematic.Connect();
    const Core::Circuit circuit = GUI::BuildCircuit(schematic.Elements, connectivity);
    REQUIRE(circuit.GetEntries().size() == 1);
    // The input ties itself to node 0 in the netlist, so its own terminal must not take that number
    CHECK(circuit.GetEntries()[0].Nodes == std::vector<int>{1});
    CHECK_FALSE(circuit.HasGround());
}

TEST_CASE("Net labels with the same name are one node without a wire between them", "[connectivity]") {
    Schematic schematic;
    schematic.Wires.emplace_back(GUI::GridPoint{0, 0}, GUI::GridPoint{4, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{20, 0}, GUI::GridPoint{24, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{40, 0}, GUI::GridPoint{44, 0});
    schematic.Label({4, 0}, "OUT");
    schematic.Label({20, 0}, "OUT");
    schematic.Label({40, 0}, "OTHER");

    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({0, 0}) == connectivity.GetNode({24, 0}));
    CHECK(connectivity.GetNode({0, 0}) != connectivity.GetNode({44, 0}));
}

TEST_CASE("Net names are compared without regard to case", "[connectivity]") {
    Schematic schematic;
    schematic.Label({0, 0}, "Vout");
    schematic.Label({10, 0}, "VOUT");
    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({0, 0}) == connectivity.GetNode({10, 0}));
}

TEST_CASE("A node is named after the first label on it, as typed", "[connectivity]") {
    Schematic schematic;
    schematic.Label({10, 0}, "Second");
    schematic.Label({0, 0}, "First");
    schematic.Wires.emplace_back(GUI::GridPoint{0, 0}, GUI::GridPoint{10, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{30, 0}, GUI::GridPoint{34, 0});

    const GUI::Connectivity connectivity = schematic.Connect();
    const int node = connectivity.GetNode({0, 0}).value();
    CHECK(connectivity.GetNetName(node) == "First");
    CHECK_FALSE(connectivity.GetNetName(connectivity.GetNode({30, 0}).value()));
    CHECK_FALSE(connectivity.GetNetName(0));
    CHECK_FALSE(connectivity.GetNetName(99));
}

TEST_CASE("A label named 0 or GND, in any case, is ground", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIGround>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    schematic.Wires.emplace_back(GUI::GridPoint{20, 0}, GUI::GridPoint{24, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{40, 0}, GUI::GridPoint{44, 0});
    schematic.Wires.emplace_back(GUI::GridPoint{60, 0}, GUI::GridPoint{64, 0});
    schematic.Label({24, 0}, "0");
    schematic.Label({44, 0}, "Gnd");
    schematic.Label({64, 0}, "GND2");

    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({20, 0}) == 0);
    CHECK(connectivity.GetNode({40, 0}) == 0);
    // Only the exact names mean ground
    CHECK(connectivity.GetNode({60, 0}) != 0);
    CHECK_FALSE(connectivity.GetNetName(0));
}

TEST_CASE("A label with no name, or an invalid one, joins nothing", "[connectivity]") {
    Schematic schematic;
    schematic.Label({0, 0}, "");
    schematic.Label({10, 0}, "");
    schematic.Label({20, 0}, "bad name");
    schematic.Label({30, 0}, "bad name");
    const GUI::Connectivity connectivity = schematic.Connect();
    CHECK(connectivity.GetNode({0, 0}) != connectivity.GetNode({10, 0}));
    CHECK(connectivity.GetNode({20, 0}) != connectivity.GetNode({30, 0}));
}

TEST_CASE("The simulator never sees a net label", "[connectivity]") {
    Schematic schematic;
    schematic.Elements.push_back(std::make_unique<GUI::UIResistor>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
    schematic.Elements.push_back(std::make_unique<GUI::UIGround>(GUI::GridPoint{2, 0}, GUI::Rotation::R0));
    schematic.Label({-2, 0}, "IN");
    schematic.Label({-8, 0}, "IN");

    const GUI::Connectivity connectivity = schematic.Connect();
    const Core::Circuit circuit = GUI::BuildCircuit(schematic.Elements, connectivity);
    CHECK(circuit.GetEntries().size() == 2);
    CHECK(circuit.ToSpiceNetlist().find("NetLabel") == std::string::npos);
}

TEST_CASE("A label named VCC joins every VCC supply, and only when there is one", "[connectivity]") {
    const auto make = [](const bool with_label) {
        Schematic schematic;
        schematic.Elements.push_back(std::make_unique<GUI::UIVCC>(GUI::GridPoint{0, 0}, GUI::Rotation::R0));
        schematic.Elements.push_back(std::make_unique<GUI::UIVCC>(GUI::GridPoint{20, 0}, GUI::Rotation::R0));
        schematic.Wires.emplace_back(GUI::GridPoint{40, 0}, GUI::GridPoint{44, 0});
        if (with_label) {
            schematic.Label({44, 0}, "vCC");
        }
        return schematic;
    };

    const Schematic labeled = make(true);
    const GUI::Connectivity joined = labeled.Connect();
    CHECK(joined.GetNode({0, 0}) == joined.GetNode({20, 0}));
    CHECK(joined.GetNode({0, 0}) == joined.GetNode({40, 0}));
    CHECK(joined.GetNode({0, 0}).value() > 0);
    CHECK(joined.GetNetName(joined.GetNode({0, 0}).value()) == "vCC");

    // Without a label, supplies stay apart
    const Schematic plain = make(false);
    const GUI::Connectivity apart = plain.Connect();
    CHECK(apart.GetNode({0, 0}) != apart.GetNode({20, 0}));
}
