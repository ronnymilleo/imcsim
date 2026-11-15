/**
 * @file    logic_outputs_tests.cpp
 * @brief   Tests for the logic output, which shows the logic level of its node and takes no part in the simulation.
 */

#include "circuit/components/logic_input.h"
#include "circuit/components/logic_output.h"
#include "digital_models.h"
#include "results/logic_outputs.h"
#include "schematic/connectivity.h"
#include "schematic/element_factory.h"
#include "schematic/ready_blocks.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace {

/// A logic output whose terminal is on the node of a logic input, and a 0 V result that knows that node.
struct OutputOnInput {
    GUI::Schematic Schematic;

    OutputOnInput() {
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::LOGIC_INPUT, {0, 0}, GUI::Rotation::R0));
        Schematic.AddElement(GUI::CreateElement(Core::ComponentType::LOGIC_OUTPUT, {4, 0}, GUI::Rotation::R0));
        Schematic.AddWire({0, 0}, {4, 0});
    }

    const GUI::UIElement &GetOutput() const { return *Schematic.GetElements()[1]; }
};

} // namespace

TEST_CASE("A logic output has no name, no value and a logic level to read against", "[logic_outputs]") {
    const Core::LogicOutput output;
    CHECK(output.GetType() == Core::ComponentType::LOGIC_OUTPUT);
    CHECK(std::string_view(output.GetNamePrefix()).empty());
    CHECK_FALSE(output.HasValue());
    CHECK(output.GetParameters().LogicLevel == 5.0);
    CHECK_FALSE(Core::LogicOutput::IsValidParameters({.LogicLevel = 0.0}));
    CHECK(Core::ParseComponentType("LogicOutput") == Core::ComponentType::LOGIC_OUTPUT);
    CHECK(output.Clone()->GetType() == Core::ComponentType::LOGIC_OUTPUT);
    CHECK(std::string_view(GUI::GetPartName(Core::ComponentType::LOGIC_OUTPUT)) == "Logic output");
    CHECK(std::ranges::contains(GUI::FindParts("logic out"), Core::ComponentType::LOGIC_OUTPUT));
}

TEST_CASE("A logic output stays out of the circuit the simulator gets", "[logic_outputs]") {
    OutputOnInput setup;
    const Core::Circuit circuit = setup.Schematic.BuildCircuit();
    REQUIRE(circuit.GetEntries().size() == 1);
    CHECK(circuit.GetEntries()[0].Part->GetType() == Core::ComponentType::LOGIC_INPUT);
    // A logic input and an output need no Ground, as the output adds nothing
    CHECK_FALSE(Core::CheckCircuit(circuit).has_value());
}

TEST_CASE("A logic output reads its node as a gate does, against its own level", "[logic_outputs]") {
    OutputOnInput setup;
    const GUI::Connectivity &connectivity = setup.Schematic.GetConnectivity();
    const int node = connectivity.GetNode({4, 0}).value_or(-1);
    REQUIRE(node > 0);
    Core::OperatingPoint point;
    point.NodeVoltages.assign(static_cast<std::size_t>(node) + 1, 0.0);
    const auto read_at = [&](const double volts) {
        point.NodeVoltages[static_cast<std::size_t>(node)] = volts;
        return GUI::ReadLogicOutput(setup.GetOutput(), connectivity, point);
    };
    CHECK(read_at(5.0) == GUI::LogicState::HIGH);
    CHECK(read_at(3.6) == GUI::LogicState::HIGH);
    CHECK(read_at(2.5) == GUI::LogicState::UNKNOWN);
    CHECK(read_at(0.2) == GUI::LogicState::LOW);

    // Against a 3.3 V level, 2.5 V is already a 1
    auto &output = static_cast<Core::LogicOutput &>(setup.Schematic.GetElement(1).GetComponent());
    output.SetParameters({.LogicLevel = 3.3});
    CHECK(read_at(2.5) == GUI::LogicState::HIGH);

    // A result without the node gives no reading
    point.NodeVoltages.resize(1);
    CHECK_FALSE(GUI::ReadLogicOutput(setup.GetOutput(), connectivity, point).has_value());
}

TEST_CASE("A logic output keeps its logic level in a file", "[logic_outputs]") {
    OutputOnInput setup;
    static_cast<Core::LogicOutput &>(setup.Schematic.GetElement(1).GetComponent()).SetParameters({.LogicLevel = 3.3});
    const auto loaded =
        GUI::LoadSchematic(GUI::SaveSchematic(setup.Schematic.GetElements(), setup.Schematic.GetWires(), {}, {}));
    REQUIRE(loaded);
    REQUIRE(loaded->Elements.size() == 2);
    const auto &output = static_cast<const Core::LogicOutput &>(loaded->Elements[1]->GetComponent());
    CHECK(output.GetType() == Core::ComponentType::LOGIC_OUTPUT);
    CHECK(output.GetParameters().LogicLevel == 3.3);
}

TEST_CASE("Logic outputs show what a decoder gives, with no LED, resistor or Ground", "[logic_outputs][digital]") {
    SKIP_WITHOUT_DIGITAL_MODELS();
    GUI::Schematic schematic;
    auto group = GUI::MakeReadyBlock(GUI::GetReadyBlocks()[3]);
    REQUIRE(group);
    schematic.MergeSubcircuits(std::move(group->Subcircuits), group->Elements);
    schematic.AddElement(std::move(group->Elements.front()));
    // Terminals: A0, A1, EN on the left, Y0 to Y3 on the right
    const std::vector<GUI::GridPoint> pins = schematic.GetElements().back()->GetTerminals();
    for (std::size_t input = 0; input < 3; ++input) {
        auto driver = GUI::CreateElement(Core::ComponentType::LOGIC_INPUT, pins[input], GUI::Rotation::R0);
        // A1 low, A0 and EN high select Y1
        static_cast<Core::LogicInput &>(driver->GetComponent()).SetHigh(input != 1);
        schematic.AddElement(std::move(driver));
    }
    for (std::size_t output = 3; output < 7; ++output) {
        schematic.AddElement(GUI::CreateElement(Core::ComponentType::LOGIC_OUTPUT, pins[output], GUI::Rotation::R0));
    }
    const Core::Circuit circuit = schematic.BuildCircuit();
    REQUIRE_FALSE(Core::CheckCircuit(circuit).has_value());
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    std::vector<std::optional<GUI::LogicState>> readings;
    for (const auto &element : schematic.GetElements()) {
        if (element->GetComponent().GetType() == Core::ComponentType::LOGIC_OUTPUT) {
            readings.push_back(GUI::ReadLogicOutput(*element, schematic.GetConnectivity(), *run.Result));
        }
    }
    CHECK(readings == std::vector<std::optional<GUI::LogicState>>{GUI::LogicState::LOW, GUI::LogicState::HIGH,
                                                                  GUI::LogicState::LOW, GUI::LogicState::LOW});
}
