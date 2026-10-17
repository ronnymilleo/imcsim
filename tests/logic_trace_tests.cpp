/**
 * @file    logic_trace_tests.cpp
 * @brief   Tests for the logic states and lanes of the Output window.
 */

#include "components/ground.h"
#include "components/logic_part.h"
#include "components/resistor.h"
#include "logic_trace.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
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

TEST_CASE("A signal splits into the stretches that keep one logic state", "[logic_trace]") {
    const std::vector<double> times = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    const std::vector<double> voltages = {0.0, 0.0, 5.0, 5.0, 2.5, 0.0};
    const std::vector<GUI::LogicSegment> segments = GUI::FindLogicSegments(times, voltages, 5.0);
    REQUIRE(segments.size() == 4);
    // Each stretch runs to where the next starts, so together they cover the whole time
    CHECK(segments[0].From == 0.0);
    CHECK(segments[0].To == 2.0);
    CHECK(segments[0].State == GUI::LogicState::Low);
    CHECK(segments[1].From == 2.0);
    CHECK(segments[1].To == 4.0);
    CHECK(segments[1].State == GUI::LogicState::High);
    CHECK(segments[2].From == 4.0);
    CHECK(segments[2].To == 5.0);
    CHECK(segments[2].State == GUI::LogicState::Unknown);
    CHECK(segments[3].From == 5.0);
    CHECK(segments[3].To == 5.0);
    CHECK(segments[3].State == GUI::LogicState::Low);
}

TEST_CASE("A signal that never changes is one segment, and no samples give none", "[logic_trace]") {
    const std::vector<double> times = {0.0, 1e-3, 2e-3};
    const std::vector<double> voltages = {4.6, 4.6, 4.6};
    const std::vector<GUI::LogicSegment> segments = GUI::FindLogicSegments(times, voltages, 5.0);
    REQUIRE(segments.size() == 1);
    CHECK(segments[0].From == 0.0);
    CHECK(segments[0].To == 2e-3);
    CHECK(segments[0].State == GUI::LogicState::High);
    CHECK(GUI::FindLogicSegments({}, {}, 5.0).empty());
}

namespace {

// A clock of 1 ms halves, high from 1 to 2 ms and 3 to 4 ms, and a signal that is high from 1 to 3 ms, over 0 to 4 ms
// sampled every 0.01 ms
struct Cells {
    std::vector<double> Times;
    std::vector<double> Clock;
    std::vector<double> Slow;

    Cells() {
        for (int index = 0; index <= 400; ++index) {
            const double time = static_cast<double>(index) * 0.01e-3;
            Times.push_back(time);
            const bool clock_high = (index >= 100 && index < 200) || index >= 300;
            Clock.push_back(clock_high ? 5.0 : 0.0);
            Slow.push_back(index >= 100 && index < 300 ? 5.0 : 0.0);
        }
    }
};

std::string Read(const std::vector<GUI::LogicSegment> &cells) {
    std::string text;
    for (const GUI::LogicSegment &cell : cells) {
        text += GUI::GetLogicStateName(cell.State);
    }
    return text;
}

} // namespace

TEST_CASE("Cells are as wide as the shortest steady stretch, so a long stretch gives several", "[logic_trace]") {
    const Cells signals;
    const std::vector<GUI::LogicSignal> list = {{signals.Clock, 5.0}, {signals.Slow, 5.0}};
    const auto cells = GUI::FindBitCells(signals.Times, list, 0.0, 4e-3);
    REQUIRE(cells.size() == 2);
    // The clock reads 0 1 0 1 and the slow signal 0 1 1 0, one reading per millisecond cell
    CHECK(Read(cells[0]) == "0101");
    CHECK(Read(cells[1]) == "0110");
    REQUIRE(cells[0].size() == 4);
    CHECK_THAT(cells[0][1].From, Catch::Matchers::WithinAbs(1e-3, 1e-9));
    CHECK_THAT(cells[0][1].To, Catch::Matchers::WithinAbs(2e-3, 1e-9));
}

TEST_CASE("Cells list only what overlaps the time asked for, and do not move with it", "[logic_trace]") {
    const Cells signals;
    const std::vector<GUI::LogicSignal> list = {{signals.Clock, 5.0}, {signals.Slow, 5.0}};
    const auto part = GUI::FindBitCells(signals.Times, list, 1.5e-3, 2.5e-3);
    // The cells of 1 to 2 ms and 2 to 3 ms, kept whole so a label can be centered on what shows of them
    CHECK(Read(part[0]) == "10");
    CHECK(Read(part[1]) == "11");
    CHECK_THAT(part[0][0].From, Catch::Matchers::WithinAbs(1e-3, 1e-9));
}

TEST_CASE("A glitch inside a cell does not show, and does not set the cell width", "[logic_trace]") {
    Cells signals;
    // A pulse of one sample just after the clock falls at 2 ms, as a hazard of a gate follows its inputs
    signals.Clock[201] = 5.0;
    const std::vector<GUI::LogicSignal> list = {{signals.Clock, 5.0}, {signals.Slow, 5.0}};
    const auto cells = GUI::FindBitCells(signals.Times, list, 0.0, 4e-3);
    CHECK(Read(cells[0]) == "0101");
}

TEST_CASE("A signal that never changes is one cell, and too few samples give none", "[logic_trace]") {
    const std::vector<double> times = {0.0, 1e-3, 2e-3};
    const std::vector<double> flat = {5.0, 5.0, 5.0};
    const std::vector<GUI::LogicSignal> list = {{flat, 5.0}};
    const auto cells = GUI::FindBitCells(times, list, 0.0, 2e-3);
    REQUIRE(cells[0].size() == 1);
    CHECK(cells[0][0].State == GUI::LogicState::High);
    CHECK(GUI::FindBitCells(std::vector<double>{0.0}, list, 0.0, 1.0)[0].empty());
}

TEST_CASE("Samples in the unknown band at the edges do not set the width of a cell", "[logic_trace]") {
    Cells signals;
    // The clock and the slow signal pass through 2.5 V for one sample at each edge, as a real edge does
    for (const std::size_t index : {100U, 200U, 300U}) {
        signals.Clock[index] = 2.5;
        signals.Slow[index] = index == 200U ? 5.0 : 2.5;
    }
    signals.Slow[300] = 2.5;
    const std::vector<GUI::LogicSignal> list = {{signals.Clock, 5.0}, {signals.Slow, 5.0}};
    const auto cells = GUI::FindBitCells(signals.Times, list, 0.0, 4e-3);
    CHECK(Read(cells[0]) == "0101");
    CHECK(Read(cells[1]) == "0110");
}

TEST_CASE("A change just after the start does not leave a sliver of a cell at the start", "[logic_trace]") {
    Cells signals;
    // The clock is already high for the first sample, then low: a start-up change, 0.01 ms in
    signals.Clock[0] = 5.0;
    signals.Slow[0] = 5.0;
    const std::vector<GUI::LogicSignal> list = {{signals.Clock, 5.0}, {signals.Slow, 5.0}};
    const auto cells = GUI::FindBitCells(signals.Times, list, 0.0, 4e-3);
    REQUIRE(cells[0].size() == 4);
    CHECK_THAT(cells[0][0].From, Catch::Matchers::WithinAbs(0.0, 1e-12));
    CHECK(Read(cells[0]) == "0101");
}
