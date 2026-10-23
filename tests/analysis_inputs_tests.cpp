/**
 * @file    analysis_inputs_tests.cpp
 * @brief   Tests for what an analysis takes from the circuit and the stored settings.
 */

#include "analysis_inputs.h"

#include "components/current_source.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include "examples.h"
#include "schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

// Opens the RC low-pass example, whose source drives node 1, its resistor joins nodes 1 and 2, and which measures both
void OpenRCFilter(GUI::Schematic &schematic) {
    const auto example = std::ranges::find(GUI::GetExamples(), "rc_low_pass_filter.imcsim", &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    schematic.RestoreMeasurements(loaded->Measurements);
}

} // namespace

TEST_CASE("The sources a DC sweep can drive come with the unit of their values", "[analysis_inputs]") {
    Core::VoltageSource voltage;
    voltage.SetName("V1");
    Core::CurrentSource current;
    current.SetName("I1");
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(voltage, {1, 0});
    circuit.Add(resistor, {1, 2});
    circuit.Add(current, {2, 0});

    const std::vector<GUI::SweepSource> sources = GUI::ListSweepSources(circuit);
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].Name == "V1");
    CHECK(std::string(sources[0].Unit) == "V");
    CHECK(sources[1].Name == "I1");
    CHECK(std::string(sources[1].Unit) == "A");
}

TEST_CASE("A sweep keeps its stored source while the circuit offers it, and falls back to the preferred one",
          "[analysis_inputs]") {
    const std::vector<GUI::SweepSource> sources = {{"V1", "V"}, {"V2", "V"}, {"I1", "A"}};
    Core::SweepRange range;
    range.Source = "V2";
    CHECK(GUI::ResolveSweepSource(range, sources, GUI::SWEPT_SOURCE_INDEX) == "V2");

    // A source that left, or none stored yet
    range.Source = "V9";
    CHECK(GUI::ResolveSweepSource(range, sources, GUI::SWEPT_SOURCE_INDEX) == "V1");
    CHECK(GUI::ResolveSweepSource(range, sources, GUI::STEPPED_SOURCE_INDEX) == "V2");
    range.Source = "";
    CHECK(GUI::ResolveSweepSource(range, sources, GUI::STEPPED_SOURCE_INDEX) == "V2");

    // The preferred one is the second, so a circuit with one source uses the only one it has
    CHECK(GUI::ResolveSweepSource(range, {{"V1", "V"}}, GUI::STEPPED_SOURCE_INDEX) == "V1");
}

TEST_CASE("An optional input is the stored source if it can still be one, else the first, else none",
          "[analysis_inputs]") {
    const std::vector<std::string> sources = {"V1", "V2"};
    CHECK(GUI::ResolveOptionalInput("V2", sources) == "V2");
    CHECK(GUI::ResolveOptionalInput("V9", sources) == "V1");
    CHECK(GUI::ResolveOptionalInput("", sources) == "V1");
    CHECK(GUI::ResolveOptionalInput("V1", {}).empty());
}

TEST_CASE("A truth table varies the stored inputs that are still there, or else all of them", "[analysis_inputs]") {
    const std::vector<std::string> available = {"V1", "V2", "V3"};
    // Circuit order, whatever the stored order, and without the ones that left
    CHECK(GUI::ResolveTruthTableInputs({"V3", "V9", "V1"}, available) == std::vector<std::string>{"V1", "V3"});
    CHECK(GUI::ResolveTruthTableInputs({}, available) == available);
    CHECK(GUI::ResolveTruthTableInputs({"V9"}, available) == available);
}

TEST_CASE("The stepped parameter has the unit of its part, or degrees Celsius for the temperature",
          "[analysis_inputs]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    Core::Circuit circuit;
    circuit.Add(resistor, {1, 0});

    CHECK(GUI::GetParameterUnit(circuit, "") == "°C");
    CHECK(GUI::GetParameterUnit(circuit, "R1") == resistor.GetUnit());
    // A part that left the circuit has none, and the run reports it
    CHECK(GUI::GetParameterUnit(circuit, "R9").empty());
}

TEST_CASE("The output node is the stored point's, else a measured node that no source holds", "[analysis_inputs]") {
    GUI::Schematic schematic;
    OpenRCFilter(schematic);
    const Core::Circuit circuit = schematic.BuildCircuit();

    // The example measures the source node, which the source holds, and the node of the capacitor
    const std::optional<int> measured = GUI::ResolveOutputNode(std::nullopt, circuit, schematic);
    REQUIRE(measured.has_value());
    CHECK(schematic.IsVoltageMeasured(*measured));
    CHECK(GUI::ListMeasuredNodes(circuit, schematic).size() == 2);

    // A stored point wins over the measured nodes, and a point that left the circuit is ignored
    const std::optional<int> source_node = schematic.GetConnectivity().GetNode({4, 4});
    REQUIRE(source_node.has_value());
    CHECK(GUI::ResolveOutputNode(GUI::GridPoint{4, 4}, circuit, schematic) == source_node);
    CHECK(GUI::ResolveOutputNode(GUI::GridPoint{40, 40}, circuit, schematic) == measured);
    CHECK(*source_node != *measured);
}

TEST_CASE("A circuit with only ground has no node to measure", "[analysis_inputs]") {
    GUI::Schematic schematic;
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(ground, {0});
    CHECK_FALSE(GUI::ResolveOutputNode(std::nullopt, circuit, schematic).has_value());
    CHECK(GUI::ListMeasuredNodes(circuit, schematic).empty());
}
