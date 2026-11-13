/**
 * @file    circuit_checks_tests.cpp
 * @brief   Tests for the checks of a circuit, which no backend takes part in.
 */

#include "check_fixtures.h"
#include "circuit/components/ground.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include "simulation/circuit_checks.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

TEST_CASE("A circuit needs parts, and a ground unless it is only digital", "[circuit_checks]") {
    CHECK(Core::CheckCircuit(Core::Circuit()).value_or("").contains("empty"));

    const SourceAndResistor valid;
    CHECK_FALSE(Core::CheckCircuit(valid.Circuit).has_value());

    Core::Circuit without_ground;
    without_ground.Add(valid.Load, {1, 2});
    CHECK(Core::CheckCircuit(without_ground).value_or("").contains("no ground"));

    // Logic inputs and logic parts refer to node 0 themselves, so a circuit of only those needs no Ground part
    Core::LogicInput input;
    input.SetName("V1");
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit digital;
    digital.Add(input, {1});
    digital.Add(gate, {1, 2, 3});
    CHECK_FALSE(Core::CheckCircuit(digital).has_value());

    // A block holding only logic parts counts as digital too, but not one with a resistor inside
    auto logic_block = std::make_shared<Core::SubcircuitDefinition>();
    logic_block->Name = "logic";
    logic_block->Pins = {{"A", 1}, {"Y", 3}};
    logic_block->Body.Add(gate, {1, 2, 3});
    Core::SubcircuitInstance instance("logic", {"A", "Y"});
    instance.SetName("X1");
    Core::Circuit with_block;
    with_block.Add(input, {1});
    with_block.Add(instance, {1, 2});
    with_block.AddSubcircuit(logic_block);
    CHECK_FALSE(Core::CheckCircuit(with_block).has_value());
    logic_block->Body.Add(valid.Load, {3, 0});
    CHECK(Core::CheckCircuit(with_block).value_or("").contains("no ground"));
}
