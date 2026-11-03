/**
 * @file    subcircuit_tests.cpp
 * @brief   Tests for project-local subcircuit definitions, instances, files and ngspice runs.
 */

#include "circuit_checks.h"
#include "circuit_copy.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/vcc.h"
#include "element_factory.h"
#include "schematic.h"
#include "schematic_file.h"
#include "simulator.h"
#include "subcircuit.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

std::shared_ptr<Core::SubcircuitDefinition> MakeDivider(std::string name, Core::Resistor &upper,
                                                        Core::Resistor &lower) {
    upper.SetName("R1");
    upper.SetValueExpression("{RTOP}");
    lower.SetName("R2");
    lower.SetValueExpression("{RBOTTOM}");
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = std::move(name);
    definition->Pins = {{"IN", 1}, {"OUT", 2}, {"GND", 0}};
    definition->Parameters = {{"RTOP", 1e3}, {"RBOTTOM", 1e3}};
    definition->Body.Add(upper, {1, 2});
    definition->Body.Add(lower, {2, 0});
    return definition;
}

Core::SubcircuitInstance MakeInstance(const std::string &name, const std::string &definition) {
    Core::SubcircuitInstance instance(definition, {"IN", "OUT", "GND"});
    instance.SetName(name);
    return instance;
}

} // namespace

TEST_CASE("A subcircuit instance writes X and substitutes its parameters", "[subcircuit]") {
    Core::Resistor upper;
    Core::Resistor lower;
    const auto definition = MakeDivider("divider", upper, lower);
    auto instance = MakeInstance("X1", "divider");
    instance.SetParameter("RBOTTOM", 2e3);
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(10.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 2, 0});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(definition);

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X1 1 2 0 divider RBOTTOM=2k\n"));
    CHECK(netlist.contains(".subckt divider 1 2 0 params: RTOP=1k RBOTTOM=1k\n"));
    CHECK(netlist.contains("R1 1 2 {RTOP}\n"));
    CHECK(netlist.contains("R2 2 0 {RBOTTOM}\n"));
    CHECK(netlist.contains(".ends divider\n"));

    Core::CircuitCopy copy(circuit);
    REQUIRE(copy.GetCircuit().FindSubcircuit("divider") != nullptr);
    CHECK(copy.GetCircuit().FindSubcircuit("divider")->Body.GetEntries()[0].Part != &upper);
    const Core::OperatingPointRun run = Core::RunOperatingPoint(copy.GetCircuit());
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->NodeVoltages.size() == 3);
    CHECK(run.Result->NodeVoltages[2] == Catch::Approx(10.0 * 2.0 / 3.0).margin(1e-6));
}

TEST_CASE("Equal internal node numbers stay local to each subcircuit", "[subcircuit]") {
    Core::Resistor first_upper;
    Core::Resistor first_lower;
    Core::Resistor second_upper;
    Core::Resistor second_lower;
    auto first = MakeDivider("first", first_upper, first_lower);
    auto second = MakeDivider("second", second_upper, second_lower);
    auto first_instance = MakeInstance("X1", "first");
    auto second_instance = MakeInstance("X2", "second");
    first_instance.SetParameter("RBOTTOM", 1e3);
    second_instance.SetParameter("RBOTTOM", 3e3);
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(8.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(first_instance, {1, 2, 0});
    circuit.Add(second_instance, {1, 3, 0});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(first);
    circuit.AddSubcircuit(second);

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains(".subckt first 1 2 0"));
    CHECK(netlist.contains(".subckt second 1 2 0"));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result);
    CHECK(run.Result->NodeVoltages[2] == Catch::Approx(4.0).margin(1e-6));
    CHECK(run.Result->NodeVoltages[3] == Catch::Approx(6.0).margin(1e-6));
}

TEST_CASE("Custom models with equal names are scoped by definition", "[subcircuit]") {
    Core::Resistor first_resistor;
    first_resistor.SetName("R1");
    Core::Resistor second_resistor;
    second_resistor.SetName("R1");
    Core::Diode first_diode(Core::ComponentType::DIODE);
    first_diode.SetName("D1");
    first_diode.SetCustomParameters({.SaturationCurrent = 1e-12});
    Core::Diode second_diode(Core::ComponentType::DIODE);
    second_diode.SetName("D1");
    second_diode.SetCustomParameters({.SaturationCurrent = 1e-15});

    auto first = std::make_shared<Core::SubcircuitDefinition>();
    first->Name = "first";
    first->Pins = {{"IN", 1}, {"GND", 0}};
    first->Body.Add(first_resistor, {1, 2});
    first->Body.Add(first_diode, {2, 0});
    auto second = std::make_shared<Core::SubcircuitDefinition>(*first);
    second->Name = "second";
    second->Body = Core::Circuit();
    second->Body.Add(second_resistor, {1, 2});
    second->Body.Add(second_diode, {2, 0});

    Core::SubcircuitInstance first_instance("first", {"IN", "GND"});
    first_instance.SetName("X1");
    Core::SubcircuitInstance second_instance("second", {"IN", "GND"});
    second_instance.SetName("X2");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(first_instance, {1, 0});
    circuit.Add(second_instance, {1, 0});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(first);
    circuit.AddSubcircuit(second);

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("D1 2 0 imcsim_first_DCUSTOM_D1\n"));
    CHECK(netlist.contains(".model imcsim_first_DCUSTOM_D1 D("));
    CHECK(netlist.contains("D1 2 0 imcsim_second_DCUSTOM_D1\n"));
    CHECK(netlist.contains(".model imcsim_second_DCUSTOM_D1 D("));
    CHECK_FALSE(netlist.contains(".model DCUSTOM_D1 "));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    REQUIRE(run.Result);
}

TEST_CASE("Subcircuit definitions and instances round-trip through an imcsim file", "[subcircuit]") {
    GUI::SavedSubcircuit definition;
    definition.Name = "divider";
    definition.Parameters = {{"RTOP", 1e3}, {"RBOTTOM", 2e3}};
    definition.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    definition.Elements.back()->GetComponent().SetName("R1");
    definition.Elements.back()->GetComponent().SetValueExpression("{RTOP}");
    definition.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 4}, GUI::Rotation::R0));
    definition.Elements.back()->GetComponent().SetName("R2");
    definition.Elements.back()->GetComponent().SetValueExpression("{RBOTTOM}");
    definition.Wires = {{{2, 0}, {4, 0}}, {{4, 0}, {4, 4}}, {{4, 4}, {2, 4}}};
    definition.Pins = {{"IN", {-2, 0}}, {"OUT", {2, 0}}, {"GND", {-2, 4}}};

    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {10, 0}, GUI::Rotation::R0));
    auto &instance = static_cast<Core::SubcircuitInstance &>(elements.back()->GetComponent());
    instance.SetName("X1");
    instance.SetDefinitionName("divider");
    instance.SetPins({"IN", "OUT", "GND"});
    instance.SetParameter("RTOP", 3e3);
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(std::move(definition));

    const std::string saved = GUI::SaveSchematic(elements, {}, {}, {}, definitions);
    CHECK(saved.contains("\"subcircuits\""));
    CHECK(saved.contains("\"value\": \"{RTOP}\""));
    auto loaded = GUI::LoadSchematic(saved);
    REQUIRE(loaded);
    CHECK(loaded->Warnings.empty());
    REQUIRE(loaded->Subcircuits.size() == 1);
    CHECK(loaded->Subcircuits[0].Name == "divider");
    REQUIRE(loaded->Subcircuits[0].Pins.size() == 3);
    CHECK(loaded->Subcircuits[0].Pins[1].Name == "OUT");
    CHECK(loaded->Subcircuits[0].Parameters == definitions[0].Parameters);
    REQUIRE(loaded->Subcircuits[0].Elements.size() == 2);
    CHECK(loaded->Subcircuits[0].Elements[0]->GetComponent().GetValueExpression() == "{RTOP}");
    REQUIRE(loaded->Elements.size() == 1);
    const auto &copy = static_cast<const Core::SubcircuitInstance &>(loaded->Elements[0]->GetComponent());
    CHECK(copy.GetDefinitionName() == "divider");
    CHECK(copy.GetPins() == std::vector<std::string>{"IN", "OUT", "GND"});
    CHECK(copy.GetParameters().at("RTOP") == 3e3);

    GUI::Schematic schematic;
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings),
                      std::move(loaded->Subcircuits));
    const std::string netlist = schematic.BuildSpiceNetlist();
    INFO(netlist);
    CHECK(netlist.contains("X1 1 2 3 divider RTOP=3k\n"));
    CHECK(netlist.contains(".subckt divider 1 3 2 params: RTOP=1k RBOTTOM=2k\n"));
    CHECK(netlist.contains("R1 1 3 {RTOP}\n"));
    CHECK(netlist.contains("R2 2 3 {RBOTTOM}\n"));
}

TEST_CASE("An instance with an unknown definition is rejected clearly", "[subcircuit]") {
    auto instance = MakeInstance("X1", "missing");
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(instance, {1, 2, 0});
    circuit.Add(ground, {0});
    const std::optional<std::string> problem = Core::CheckCircuit(circuit);
    REQUIRE(problem);
    CHECK(problem->contains("unknown subcircuit missing"));

    const auto loaded = GUI::LoadSchematic(R"({"format":"imcsim-schematic","version":1,
        "elements":[{"type":"Subcircuit","name":"X1","definition":"missing","x":0,"y":0,"rotation":0}]})");
    REQUIRE(loaded);
    CHECK(loaded->Elements.empty());
    REQUIRE(loaded->Warnings.size() == 1);
    CHECK(loaded->Warnings[0].contains("unknown subcircuit"));
}
