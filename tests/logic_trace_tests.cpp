/**
 * @file    logic_trace_tests.cpp
 * @brief   Tests for the logic states and lanes of the Output window.
 */

#include "components/ground.h"
#include "components/logic_part.h"
#include "components/resistor.h"
#include "logic_trace.h"
#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("A voltage reads 1 above 70% of the logic level, 0 below 30% of it, and unknown between", "[logic_trace]") {
    CHECK(GUI::ReadLogicState(5.0, 5.0) == GUI::LogicState::High);
    CHECK(GUI::ReadLogicState(3.5, 5.0) == GUI::LogicState::High);
    CHECK(GUI::ReadLogicState(3.4, 5.0) == GUI::LogicState::Unknown);
    CHECK(GUI::ReadLogicState(2.5, 5.0) == GUI::LogicState::Unknown);
    CHECK(GUI::ReadLogicState(1.6, 5.0) == GUI::LogicState::Unknown);
    CHECK(GUI::ReadLogicState(1.5, 5.0) == GUI::LogicState::Low);
    CHECK(GUI::ReadLogicState(0.0, 5.0) == GUI::LogicState::Low);
    // The thresholds follow the level
    CHECK(GUI::ReadLogicState(2.4, 3.3) == GUI::LogicState::High);
    CHECK(GUI::ReadLogicState(2.4, 5.0) == GUI::LogicState::Unknown);
}

TEST_CASE("A logic state is named as a logic analyzer writes it", "[logic_trace]") {
    CHECK(std::string_view(GUI::GetLogicStateName(GUI::LogicState::Low)) == "0");
    CHECK(std::string_view(GUI::GetLogicStateName(GUI::LogicState::High)) == "1");
    CHECK(std::string_view(GUI::GetLogicStateName(GUI::LogicState::Unknown)) == "X");
}

TEST_CASE("A lane lays the three states at three heights above the bottom of its lane", "[logic_trace]") {
    const std::vector<double> voltages = {0.0, 5.0, 2.5, 4.6};
    const std::vector<double> bottom = GUI::BuildLane(voltages, 5.0, 0);
    REQUIRE(bottom.size() == voltages.size());
    CHECK(bottom[0] == GUI::LaneLowOffset);
    CHECK(bottom[1] == GUI::LaneHighOffset);
    CHECK(bottom[2] == GUI::LaneUnknownOffset);
    // A high that a load pulls below the level is still high
    CHECK(bottom[3] == GUI::LaneHighOffset);

    // Each lane stands one stride above the one below it, and a low lane's high stays below the next lane's low
    const std::vector<double> second = GUI::BuildLane(voltages, 5.0, 1);
    CHECK(second[0] == GUI::LaneStride + GUI::LaneLowOffset);
    CHECK(second[1] == GUI::LaneStride + GUI::LaneHighOffset);
    CHECK(GUI::LaneHighOffset < GUI::LaneStride);
}

TEST_CASE("The logic nodes are those a gate or flip-flop is connected to, with their largest level", "[logic_trace]") {
    Core::LogicPart gate(Core::ComponentType::And);
    gate.SetName("U1");
    Core::LogicPart inverter(Core::ComponentType::Not);
    inverter.SetName("U2");
    inverter.SetParameters({.LogicLevel = 3.3, .PropagationDelay = 10e-9});
    Core::Resistor resistor;
    resistor.SetName("R1");
    const Core::Ground reference;
    // The gate's inputs on nodes 1 and 2, its output on node 3, which the inverter reads, then its output on node 4;
    // the resistor hangs on node 4 and node 5, and ground is node 0
    Core::Circuit circuit;
    circuit.Add(gate, {1, 2, 3});
    circuit.Add(inverter, {3, 4});
    circuit.Add(resistor, {4, 5});
    circuit.Add(reference, {0});

    const std::vector<GUI::LogicNode> nodes = GUI::FindLogicNodes(circuit);
    REQUIRE(nodes.size() == 4);
    CHECK(nodes[0].Node == 1);
    CHECK(nodes[0].LogicLevel == 5.0);
    CHECK(nodes[2].Node == 3);
    // Shared by two parts of different levels, the larger
    CHECK(nodes[2].LogicLevel == 5.0);
    CHECK(nodes[3].Node == 4);
    CHECK(nodes[3].LogicLevel == 3.3);
}

TEST_CASE("A circuit with no logic part has no logic nodes", "[logic_trace]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});
    CHECK(GUI::FindLogicNodes(circuit).empty());
}
