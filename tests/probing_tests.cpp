/**
 * @file    probing_tests.cpp
 * @brief   Tests for what the Probe tool measures at a point of the schematic.
 */

#include "element_factory.h"
#include "probing.h"
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
        Elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {0, 0}, GUI::Rotation::R0));
        Elements.back()->GetComponent().SetName("R1");
        Elements.push_back(GUI::CreateElement(Core::ComponentType::NPN, {10, 10}, GUI::Rotation::R0));
        Elements.back()->GetComponent().SetName("Q1");
        Elements.push_back(GUI::CreateElement(Core::ComponentType::Ground, {0, 8}, GUI::Rotation::R0));
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
    CHECK(GUI::GetMeasurementLabel(*target) == "I(R1)");
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
    CHECK(GUI::GetMeasurementLabel(*target) == std::format("V({})", *target->Node));
}

TEST_CASE("Probing ground, its wires or empty space measures nothing", "[probing]") {
    const ProbedSchematic schematic;
    CHECK_FALSE(schematic.At({0.0f, 8.5f}));
    CHECK_FALSE(schematic.At({-2.0f, 8.0f}));
    CHECK_FALSE(schematic.At({20.0f, 20.0f}));
}
