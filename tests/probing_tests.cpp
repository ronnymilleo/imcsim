/**
 * @file    probing_tests.cpp
 * @brief   Tests for what the Probe tool measures at a point of the schematic.
 */

#include "circuit/components/net_label.h"
#include "schematic/element_factory.h"
#include "schematic/probing.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <format>
#include <memory>
#include <vector>

namespace {

/**
 * @struct  ProbedSchematic
 * @brief   A resistor from (-2, 0) to (2, 0) with a wire on to (6, 0), an NPN at (10, 10), and a ground at (0, 8)
 *          with a wire from (-4, 8) to it.
 */
struct ProbedSchematic {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires;

    ProbedSchematic() {
        Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
        Elements.back()->GetComponent().SetName("R1");
        Elements.push_back(GUI::CreateElement(Core::ComponentType::NPN, {10, 10}, GUI::Rotation::R0));
        Elements.back()->GetComponent().SetName("Q1");
        Elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {0, 8}, GUI::Rotation::R0));
        Wires.emplace_back(GUI::GridPoint{2, 0}, GUI::GridPoint{6, 0});
        Wires.emplace_back(GUI::GridPoint{-4, 8}, GUI::GridPoint{0, 8});
    }

    std::optional<GUI::MeasurementTarget> At(const ImVec2 world_pos) const {
        const GUI::Connectivity connectivity(Elements, Wires);
        return GUI::FindMeasurementTarget(Elements, Wires, connectivity, world_pos, 0.25f);
    }
};

} // namespace

TEST_CASE("Probing a part measures its current", "[probing]") {
    const ProbedSchematic schematic;
    const auto target = schematic.At({0.0f, 0.2f});
    REQUIRE(target);
    CHECK_FALSE(target->Node);
    CHECK(target->Current == "R1");
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    CHECK(GUI::GetMeasurementLabel(*target, connectivity) == "I(R1)");
}

TEST_CASE("Probing a transistor measures the current of its nearest terminal", "[probing]") {
    const ProbedSchematic schematic;
    // Collector at (11, 8), base at (8, 10), emitter at (11, 12)
    CHECK(schematic.At({10.8f, 8.4f})->Current == "Q1.C");
    CHECK(schematic.At({8.4f, 10.0f})->Current == "Q1.B");
    CHECK(schematic.At({10.8f, 11.6f})->Current == "Q1.E");
}

TEST_CASE("Probing a wire measures the voltage of its node", "[probing]") {
    const ProbedSchematic schematic;
    const auto target = schematic.At({4.0f, 0.1f});
    REQUIRE(target);
    REQUIRE(target->Node);
    CHECK(*target->Node > 0);
    CHECK(target->Current.empty());
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    CHECK(GUI::GetMeasurementLabel(*target, connectivity) == std::format("V({})", *target->Node));
}

TEST_CASE("Probing ground, its wires or empty space measures nothing", "[probing]") {
    const ProbedSchematic schematic;
    CHECK_FALSE(schematic.At({0.0f, 8.5f}));
    CHECK_FALSE(schematic.At({-2.0f, 8.0f}));
    CHECK_FALSE(schematic.At({20.0f, 20.0f}));
}

TEST_CASE("A node a net label named is measured and labeled by that name", "[probing]") {
    ProbedSchematic schematic;
    schematic.Elements.push_back(GUI::CreateElement(Core::ComponentType::NET_LABEL, {6, 0}, GUI::Rotation::R0));
    static_cast<Core::NetLabel &>(schematic.Elements.back()->GetComponent()).SetNet("VOUT");

    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const auto target = schematic.At({4.0f, 0.1f});
    REQUIRE(target);
    REQUIRE(target->Node);
    CHECK(GUI::GetMeasurementLabel(*target, connectivity) == "V(VOUT)");
    CHECK(GUI::GetVoltageLabel(connectivity, *target->Node) == "V(VOUT)");
    // The other nodes keep their numbers
    CHECK(GUI::GetVoltageLabel(connectivity, *target->Node + 1) == std::format("V({})", *target->Node + 1));
}

TEST_CASE("Probing a relay or a transformer picks the current of the winding nearest the point", "[probing]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RELAY, {0, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("K1");
    elements.push_back(GUI::CreateElement(Core::ComponentType::TRANSFORMER, {20, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("T1");
    const std::vector<GUI::UIWire> wires;
    const GUI::Connectivity connectivity(elements, wires);
    const auto at = [&](const ImVec2 position) {
        return GUI::FindMeasurementTarget(elements, wires, connectivity, position, 0.25f);
    };

    // The contact terminals are at the top and bottom, and the coil pins on the left
    const auto contact = at({0.0f, -1.5f});
    REQUIRE(contact);
    CHECK(contact->Current == "K1.S");
    const auto coil = at({-1.5f, -1.0f});
    REQUIRE(coil);
    CHECK(coil->Current == "K1.C");
    // The primary is on the left of a transformer and the secondary on the right
    const auto primary = at({18.5f, 0.0f});
    REQUIRE(primary);
    CHECK(primary->Current == "T1.P");
    const auto secondary = at({21.5f, 0.0f});
    REQUIRE(secondary);
    CHECK(secondary->Current == "T1.S");
}

TEST_CASE("A measurement reads its value in the operating point, and names it with the value", "[probing]") {
    const ProbedSchematic schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const int node = *schematic.At({4.0f, 0.0f})->Node;
    Core::OperatingPoint operating_point;
    operating_point.NodeVoltages.assign(static_cast<std::size_t>(node) + 1, 0.0);
    operating_point.NodeVoltages[static_cast<std::size_t>(node)] = 4.5;
    operating_point.Currents.push_back({.Name = "R1", .Current = 2e-3});

    const GUI::MeasurementTarget voltage = {.Node = node, .Current = {}};
    const GUI::MeasurementTarget current = {.Node = std::nullopt, .Current = "R1"};
    CHECK(GUI::FindMeasuredValue(voltage, operating_point) == 4.5);
    CHECK(GUI::FindMeasuredValue(current, operating_point) == 2e-3);
    CHECK(GUI::DescribeMeasurement(voltage, connectivity, &operating_point) == std::format("V({}) = 4.5V", node));
    CHECK(GUI::DescribeMeasurement(current, connectivity, &operating_point) == "I(R1) = 2mA");
}

TEST_CASE("A measurement the operating point lacks, or one before any run, is named without a value", "[probing]") {
    const ProbedSchematic schematic;
    const GUI::Connectivity connectivity(schematic.Elements, schematic.Wires);
    const Core::OperatingPoint operating_point;
    const GUI::MeasurementTarget voltage = {.Node = 3, .Current = {}};
    const GUI::MeasurementTarget current = {.Node = std::nullopt, .Current = "Q1.C"};

    CHECK_FALSE(GUI::FindMeasuredValue(voltage, operating_point));
    CHECK_FALSE(GUI::FindMeasuredValue(current, operating_point));
    CHECK(GUI::DescribeMeasurement(voltage, connectivity, &operating_point) == "V(3)");
    CHECK(GUI::DescribeMeasurement(current, connectivity, nullptr) == "I(Q1.C)");
}
