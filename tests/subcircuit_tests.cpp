/**
 * @file    subcircuit_tests.cpp
 * @brief   Tests for project-local subcircuit definitions, instances, files and ngspice runs.
 */

#include "circuit_checks.h"
#include "circuit_copy.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/net_label.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/vcc.h"
#include "element_factory.h"
#include "schematic.h"
#include "schematic_file.h"
#include "simulator.h"
#include "subcircuit.h"
#include "subcircuit_pins.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
    // The lower resistor reaches ground inside the block, which every part of the netlist shares
    definition->Pins = {{"IN", 1}, {"OUT", 2}};
    definition->Parameters = {{"RTOP", 1e3}, {"RBOTTOM", 1e3}};
    definition->Body.Add(upper, {1, 2});
    definition->Body.Add(lower, {2, 0});
    return definition;
}

Core::SubcircuitInstance MakeInstance(const std::string &name, const std::string &definition) {
    Core::SubcircuitInstance instance(definition, {"IN", "OUT"});
    instance.SetName(name);
    return instance;
}

// A pin marker; unturned, its arrow points right, which puts the pin on the left of the block
void AddMarker(GUI::Schematic &schematic, const GUI::GridPoint point, const std::string &name,
               const GUI::Rotation rotation = GUI::Rotation::R0) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::PIN, point, rotation));
    static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet(name);
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
    circuit.Add(instance, {1, 2});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(definition);

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains("X1 1 2 divider RBOTTOM=2k\n"));
    CHECK(netlist.contains(".subckt divider 1 2 params: RTOP=1k RBOTTOM=1k\n"));
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
    circuit.Add(first_instance, {1, 2});
    circuit.Add(second_instance, {1, 3});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(first);
    circuit.AddSubcircuit(second);

    const std::string netlist = circuit.ToSpiceNetlist();
    CHECK(netlist.contains(".subckt first 1 2 params:"));
    CHECK(netlist.contains(".subckt second 1 2 params:"));
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
    first->Pins = {{"IN", 1}};
    first->Body.Add(first_resistor, {1, 2});
    first->Body.Add(first_diode, {2, 0});
    auto second = std::make_shared<Core::SubcircuitDefinition>(*first);
    second->Name = "second";
    second->Body = Core::Circuit();
    second->Body.Add(second_resistor, {1, 2});
    second->Body.Add(second_diode, {2, 0});

    Core::SubcircuitInstance first_instance("first", {"IN"});
    first_instance.SetName("X1");
    Core::SubcircuitInstance second_instance("second", {"IN"});
    second_instance.SetName("X2");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(first_instance, {1});
    circuit.Add(second_instance, {1});
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

    auto content = schematic.OpenSubcircuit("divider");
    REQUIRE(content);
    auto markers = (*content)->GetSubcircuitPinMarkers();
    REQUIRE(markers);
    REQUIRE(markers->size() == 3);
    CHECK((*markers)[0].SuggestedName == "IN");
    CHECK((*markers)[1].SuggestedName == "OUT");
    CHECK((*markers)[2].SuggestedName == "GND");
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

TEST_CASE("Subcircuit instances clear parameter overrides and validate identifiers", "[subcircuit]") {
    auto instance = MakeInstance("X1", "divider");
    instance.SetParameter("RTOP", 3e3);
    REQUIRE_FALSE(instance.GetParameters().empty());
    instance.ClearParameters();
    CHECK(instance.GetParameters().empty());

    CHECK(Core::SubcircuitInstance::IsValidIdentifier("divider_2"));
    CHECK(Core::SubcircuitInstance::IsValidIdentifier("_divider"));
    CHECK_FALSE(Core::SubcircuitInstance::IsValidIdentifier(""));
    CHECK_FALSE(Core::SubcircuitInstance::IsValidIdentifier("2divider"));
    CHECK_FALSE(Core::SubcircuitInstance::IsValidIdentifier("two-words"));
}

TEST_CASE("A selection becomes a reusable block and its creation is undoable", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {-8, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({-6, 0}, {-2, 0});
    schematic.SetSelection({0, 3, 4}, {0, 1});
    schematic.CommitUndoStep();

    auto candidates = schematic.GetSubcircuitPinCandidates();
    REQUIRE(candidates);
    REQUIRE(candidates->size() == 2);
    CHECK((*candidates)[0].Point == GUI::GridPoint{-2, 0});
    CHECK((*candidates)[0].SuggestedName == "IN");
    auto created =
        schematic.CreateSubcircuitFromSelection("block", {{"INPUT", {-2, 0}}, {"OUTPUT", {2, 0}}}, {{"R", 2e3}});
    REQUIRE(created);
    REQUIRE(schematic.GetSubcircuits().size() == 1);
    CHECK(schematic.GetSubcircuits()[0].Elements.size() == 3);
    CHECK(schematic.GetSubcircuits()[0].Pins[0].Name == "INPUT");
    CHECK(static_cast<const Core::NetLabel &>(schematic.GetSubcircuits()[0].Elements[1]->GetComponent()).GetNet() ==
          "INPUT");
    CHECK(static_cast<const Core::NetLabel &>(schematic.GetSubcircuits()[0].Elements[2]->GetComponent()).GetNet() ==
          "OUTPUT");
    REQUIRE(schematic.GetElements().size() == 3);
    const auto &instance = static_cast<const Core::SubcircuitInstance &>(schematic.GetElement(2).GetComponent());
    CHECK(instance.GetDefinitionName() == "block");
    CHECK(instance.GetPins() == std::vector<std::string>{"INPUT", "OUTPUT"});
    CHECK(schematic.GetWires().size() > 1);

    schematic.CommitUndoStep();
    schematic.Undo();
    REQUIRE(schematic.GetElements().size() == 5);
    CHECK(schematic.GetElement(0).GetComponent().GetType() == Core::ComponentType::RESISTOR);
    CHECK(schematic.GetSubcircuits().empty());
    schematic.Redo();
    REQUIRE(schematic.GetSubcircuits().size() == 1);
    CHECK(schematic.GetElement(2).GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT);
}

TEST_CASE("Only selected pin markers become block connections", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {-8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 8}, GUI::Rotation::R90));
    AddMarker(schematic, {-2, 0}, "");
    AddMarker(schematic, {2, 0}, "", GUI::Rotation::R180);
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({-6, 0}, {-2, 0});
    schematic.AddWire({0, 2}, {0, 6});
    schematic.SetSelection({0, 4, 5}, {0, 1, 2});

    auto candidates = schematic.GetSubcircuitPinCandidates();
    REQUIRE(candidates);
    REQUIRE(candidates->size() == 2);
    CHECK((*candidates)[0].SuggestedName == "P1");
    CHECK((*candidates)[1].SuggestedName == "P2");
    REQUIRE(schematic.CreateSubcircuitFromSelection("amplifier", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    CHECK(schematic.GetSubcircuits()[0].Pins.size() == 2);
    CHECK(static_cast<const Core::SubcircuitInstance &>(schematic.GetElements().back()->GetComponent()).GetPins() ==
          std::vector<std::string>{"IN", "OUT"});
}

TEST_CASE("Block creation requires unique markers on separate nodes", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.SetSelection({0}, {});
    auto unmarked = schematic.GetSubcircuitPinCandidates();
    REQUIRE_FALSE(unmarked);
    CHECK(unmarked.error().contains("pin markers"));
    CHECK_FALSE(schematic.CreateSubcircuitFromSelection("block", {}));

    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "IN", GUI::Rotation::R180);
    schematic.SetSelection({0, 1, 2}, {});
    auto repeated = schematic.GetSubcircuitPinCandidates();
    REQUIRE_FALSE(repeated);
    CHECK(repeated.error().contains("unique"));

    static_cast<Core::NetLabel &>(schematic.GetElement(2).GetComponent()).SetNet("OUT");
    schematic.AddWire({-2, 0}, {2, 0});
    auto same_node = schematic.GetSubcircuitPinCandidates();
    REQUIRE_FALSE(same_node);
    CHECK(same_node.error().contains("one pin marker"));
}

TEST_CASE("Editing a definition propagates pins, parameters and contents to every instance", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {-8, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({-6, 0}, {-2, 0});
    schematic.SetSelection({0, 3, 4}, {});
    auto created = schematic.CreateSubcircuitFromSelection("block", {{"IN", {-2, 0}}, {"OUT", {2, 0}}},
                                                           {{"R", 1e3}, {"DROP", 2.0}});
    REQUIRE(created);
    auto &first_instance = static_cast<Core::SubcircuitInstance &>(schematic.GetElement(2).GetComponent());
    first_instance.SetParameter("R", 3e3);
    first_instance.SetParameter("DROP", 3.0);
    schematic.MarkModified();
    schematic.CommitUndoStep();

    auto second = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {20, 0}, GUI::Rotation::R0);
    auto &second_instance = static_cast<Core::SubcircuitInstance &>(second->GetComponent());
    second_instance.SetDefinitionName("block");
    second_instance.SetPins({"IN", "OUT"});
    second_instance.SetParameter("R", 4e3);
    second_instance.SetParameter("DROP", 3.0);
    schematic.AddElement(std::move(second));
    schematic.CommitUndoStep();

    auto content = schematic.OpenSubcircuit("block");
    REQUIRE(content);
    REQUIRE((*content)->GetElements().size() == 3);
    (*content)->GetElement(0).GetComponent().SetValueExpression("{R}");
    (*content)->SelectElement(1);
    (*content)->DeleteSelection();
    auto updated = schematic.UpdateSubcircuit("block", **content, {{"OUT", {2, 0}}}, {{"R", 1e3}});
    REQUIRE(updated);
    CHECK(schematic.GetSubcircuits()[0].Pins.size() == 1);
    CHECK(schematic.GetSubcircuits()[0].Elements[0]->GetComponent().GetValueExpression() == "{R}");
    CAPTURE(schematic.GetWires());
    // OUT is an output, so its terminal sits on the right of the block
    CHECK(schematic.GetWires() == std::vector<GUI::UIWire>{{{2, 0}, {6, 0}}, {{-6, 0}, {-2, 0}}, {{2, 0}, {3, 0}}});
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    const std::vector<GUI::GridPoint> moved_terminals = schematic.GetElement(2).GetTerminals();
    REQUIRE(moved_terminals.size() == 1);
    CHECK(nodes.GetNode({6, 0}) == nodes.GetNode(moved_terminals[0]));
    for (const auto &element : schematic.GetElements()) {
        if (element->GetComponent().GetType() != Core::ComponentType::SUBCIRCUIT) {
            continue;
        }
        const auto &instance = static_cast<const Core::SubcircuitInstance &>(element->GetComponent());
        CHECK(instance.GetPins() == std::vector<std::string>{"OUT"});
        CHECK(instance.GetParameters().contains("R"));
        CHECK_FALSE(instance.GetParameters().contains("DROP"));
    }

    schematic.CommitUndoStep();
    schematic.Undo();
    CHECK(schematic.GetSubcircuits()[0].Pins.size() == 2);
    CHECK(static_cast<const Core::SubcircuitInstance &>(schematic.GetElement(2).GetComponent()).GetPins().size() == 2);
    schematic.Redo();
    CHECK(schematic.GetSubcircuits()[0].Pins.size() == 1);
}

TEST_CASE("Block creation rejects nested blocks and invalid metadata", "[subcircuit]") {
    GUI::Schematic schematic;
    CHECK_FALSE(schematic.GetSubcircuitPinCandidates());
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0));
    schematic.SetSelection({0}, {});
    CHECK_FALSE(schematic.GetSubcircuitPinCandidates());
    CHECK_FALSE(schematic.CreateSubcircuitFromSelection("not valid", {}));
    CHECK_FALSE(schematic.OpenSubcircuit("missing"));
}

TEST_CASE("A pin on the ground inside a block is refused, since it would connect nothing", "[subcircuit]") {
    // ngspice keeps node 0 global, so a port on it leaves the outer net unconnected
    Core::Resistor resistor;
    resistor.SetName("R1");
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "grounded";
    definition->Pins = {{"IN", 1}, {"GND", 0}};
    definition->Body.Add(resistor, {1, 0});
    Core::SubcircuitInstance instance("grounded", {"IN", "GND"});
    instance.SetName("X1");
    Core::VCC supply;
    supply.SetName("V1");
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 0});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(definition);
    const std::optional<std::string> problem = Core::CheckCircuit(circuit);
    REQUIRE(problem);
    CHECK(problem->contains("ground"));

    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {2, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "GND", GUI::Rotation::R180);
    schematic.SetSelection({0, 1, 2, 3}, {});
    const auto created = schematic.CreateSubcircuitFromSelection("grounded", {{"IN", {-2, 0}}, {"GND", {2, 0}}});
    REQUIRE_FALSE(created);
    CHECK(created.error().contains("ground"));
    CHECK(schematic.GetSubcircuits().empty());
    CHECK(schematic.GetElements().size() == 4);

    GUI::SavedSubcircuit saved;
    saved.Name = "grounded";
    saved.Pins = {{"IN", {-2, 0}}, {"GND", {2, 0}}};
    saved.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    saved.Elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {2, 0}, GUI::Rotation::R0));
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(std::move(saved));
    const auto loaded = GUI::LoadSchematic(GUI::SaveSchematic({}, {}, {}, {}, definitions));
    REQUIRE(loaded);
    CHECK(loaded->Subcircuits.empty());
    REQUIRE(loaded->Warnings.size() == 1);
    CHECK(loaded->Warnings[0].contains("ground"));
}

TEST_CASE("Reordering the pins of a new block keeps each name on its own marker", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.SetSelection({0, 1, 2}, {});
    // The output first, as the dialog's Up button leaves them
    REQUIRE(schematic.CreateSubcircuitFromSelection("block", {{"OUT", {2, 0}}, {"IN", {-2, 0}}}));

    const GUI::SavedSubcircuit &definition = schematic.GetSubcircuits()[0];
    REQUIRE(definition.Pins.size() == 2);
    CHECK(definition.Pins[0] == GUI::SavedSubcircuitPin{"OUT", {2, 0}});
    CHECK(definition.Pins[1] == GUI::SavedSubcircuitPin{"IN", {-2, 0}});
    for (const auto &element : definition.Elements) {
        if (!Core::IsSubcircuitPin(element->GetComponent().GetType())) {
            continue;
        }
        const std::string &net = static_cast<const Core::NetLabel &>(element->GetComponent()).GetNet();
        CHECK(net == (element->GetTerminals().front() == GUI::GridPoint{2, 0} ? "OUT" : "IN"));
    }
    // The output is the first terminal, and still on the right of the box
    const GUI::UIElement &block = *schematic.GetElements().back();
    const auto &instance = static_cast<const Core::SubcircuitInstance &>(block.GetComponent());
    CHECK(instance.GetPins() == std::vector<std::string>{"OUT", "IN"});
    CHECK(instance.GetPinSide("OUT") == Core::PinSide::RIGHT);
    CHECK(instance.GetPinSide("IN") == Core::PinSide::LEFT);
    const std::vector<GUI::GridPoint> terminals = block.GetTerminals();
    REQUIRE(terminals.size() == 2);
    CHECK(terminals[0].X > block.GetPosition().X);
    CHECK(terminals[1].X < block.GetPosition().X);
}

TEST_CASE("A block puts each pin on the edge its marker gave it, centered along that edge", "[subcircuit]") {
    auto element = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0);
    auto &instance = static_cast<Core::SubcircuitInstance &>(element->GetComponent());
    instance.SetPins({"Q", "A", "VP", "B", "VN"});
    instance.SetPinSides({{"Q", Core::PinSide::RIGHT},
                          {"A", Core::PinSide::LEFT},
                          {"VP", Core::PinSide::TOP},
                          {"B", Core::PinSide::LEFT},
                          {"VN", Core::PinSide::BOTTOM}});
    const std::vector<GUI::GridPoint> terminals = element->GetTerminals();
    // Two pins on the left make the box two units tall each way, plus one for the names of the pins on top and below,
    // which keep it two wide
    CHECK(terminals == std::vector<GUI::GridPoint>{{3, 0}, {-3, -1}, {0, -4}, {-3, 1}, {0, 4}});

    // Four pins on top widen the box, and stay two units apart
    instance.SetPins({"A", "B", "C", "D"});
    instance.SetPinSides(
        {{"A", Core::PinSide::TOP}, {"B", Core::PinSide::TOP}, {"C", Core::PinSide::TOP}, {"D", Core::PinSide::TOP}});
    CHECK(element->GetTerminals() == std::vector<GUI::GridPoint>{{-3, -2}, {-1, -2}, {1, -2}, {3, -2}});
}

TEST_CASE("A pin goes on the edge its marker's arrow comes from", "[subcircuit]") {
    // The arrow points from the block's edge into the circuit
    const auto side_of = [](const GUI::Rotation rotation, const bool is_mirrored) {
        auto marker = GUI::CreateElement(Core::ComponentType::PIN, {0, 0}, rotation);
        marker->SetMirrored(is_mirrored);
        return GUI::GetMarkerSide(*marker);
    };
    CHECK(side_of(GUI::Rotation::R0, false) == Core::PinSide::LEFT);
    CHECK(side_of(GUI::Rotation::R180, false) == Core::PinSide::RIGHT);
    CHECK(side_of(GUI::Rotation::R90, false) == Core::PinSide::TOP);
    CHECK(side_of(GUI::Rotation::R270, false) == Core::PinSide::BOTTOM);
    CHECK(side_of(GUI::Rotation::R0, true) == Core::PinSide::RIGHT);

    GUI::SavedSubcircuit definition;
    definition.Name = "amplifier";
    definition.Pins = {{"VP", {0, -4}}, {"IN", {-4, 0}}};
    definition.Elements.push_back(GUI::CreateElement(Core::ComponentType::PIN, {0, -4}, GUI::Rotation::R90));
    definition.Elements.push_back(GUI::CreateElement(Core::ComponentType::PIN, {-4, 0}, GUI::Rotation::R0));
    Core::SubcircuitInstance instance;
    GUI::ApplyDefinitionPins(instance, definition);
    CHECK(instance.GetDefinitionName() == "amplifier");
    CHECK(instance.GetPins() == std::vector<std::string>{"VP", "IN"});
    CHECK(instance.GetPinSide("VP") == Core::PinSide::TOP);
    CHECK(instance.GetPinSide("IN") == Core::PinSide::LEFT);
    CHECK(GUI::FindMarkerSide(definition.Elements, {1, 1}) == std::nullopt);
    CHECK(std::string_view(GUI::GetPinSideName(Core::PinSide::BOTTOM)) == "bottom");
}

TEST_CASE("Renaming a pin keeps the wires of every instance on it", "[subcircuit]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {-8, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({-6, 0}, {-2, 0});
    schematic.SetSelection({0, 3, 4}, {});
    REQUIRE(schematic.CreateSubcircuitFromSelection("block", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    schematic.CommitUndoStep();
    const std::size_t wire_count = schematic.GetWires().size();

    auto content = schematic.OpenSubcircuit("block");
    REQUIRE(content);
    // The marker stays where it was, so the pin is the same one under a new name
    REQUIRE(schematic.UpdateSubcircuit("block", **content, {{"SIGNAL", {-2, 0}}, {"OUT", {2, 0}}}, {}));
    CHECK(schematic.GetWires().size() == wire_count);
    const GUI::UIElement &block = schematic.GetElement(2);
    CHECK(static_cast<const Core::SubcircuitInstance &>(block.GetComponent()).GetPins() ==
          std::vector<std::string>{"SIGNAL", "OUT"});
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    const std::vector<GUI::GridPoint> terminals = block.GetTerminals();
    CHECK(nodes.GetNode({-6, 0}) == nodes.GetNode(terminals[0]));
    CHECK(nodes.GetNode({6, 0}) == nodes.GetNode(terminals[1]));
}

TEST_CASE("A part follows only a parameter of the block it is in", "[subcircuit]") {
    Core::Resistor outside;
    outside.SetName("R9");
    outside.SetValueExpression("{R}");
    const Core::Ground ground;
    Core::Circuit top;
    top.Add(outside, {1, 0});
    top.Add(ground, {0});
    const std::optional<std::string> outside_problem = Core::CheckCircuit(top);
    REQUIRE(outside_problem);
    CHECK(outside_problem->contains("only a part inside a subcircuit"));

    Core::Resistor upper;
    Core::Resistor lower;
    const auto definition = MakeDivider("divider", upper, lower);
    definition->Parameters = {{"RTOP", 1e3}};
    auto instance = MakeInstance("X1", "divider");
    Core::VCC supply;
    supply.SetName("V1");
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(instance, {1, 2});
    circuit.Add(ground, {0});
    circuit.AddSubcircuit(definition);
    const std::optional<std::string> missing = Core::CheckCircuit(circuit);
    REQUIRE(missing);
    CHECK(missing->contains("{RBOTTOM}"));

    // The block editor cannot drop a parameter that a part still follows
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.SetSelection({0, 1, 2}, {});
    REQUIRE(schematic.CreateSubcircuitFromSelection("block", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}, {{"R", 1e3}}));
    auto content = schematic.OpenSubcircuit("block");
    REQUIRE(content);
    (*content)->GetElement(0).GetComponent().SetValueExpression("{R}");
    const auto updated = schematic.UpdateSubcircuit("block", **content, {{"IN", {-2, 0}}, {"OUT", {2, 0}}}, {});
    REQUIRE_FALSE(updated);
    CHECK(updated.error().contains("{R}"));
}

TEST_CASE("Only resistors, capacitors and inductors follow a parameter", "[subcircuit]") {
    Core::Resistor resistor;
    CHECK(resistor.CanFollowParameter());
    Core::VCC supply;
    CHECK_FALSE(supply.CanFollowParameter());
    supply.SetValueExpression("{V}");
    CHECK_FALSE(supply.GetValueExpression());

    const auto loaded = GUI::LoadSchematic(R"({"format":"imcsim-schematic","version":1,
        "elements":[{"type":"VCC","name":"V1","value":"{V}","x":0,"y":0,"rotation":0}]})");
    REQUIRE(loaded);
    CHECK(loaded->Elements.empty());
    REQUIRE(loaded->Warnings.size() == 1);
    CHECK(loaded->Warnings[0].contains("follows a parameter"));
}

TEST_CASE("Only the blocks in use count toward ground and the parts a run needs", "[subcircuit]") {
    Core::Resistor inner;
    inner.SetName("R1");
    const Core::Ground inner_ground;
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "load";
    definition->Pins = {{"IN", 1}};
    definition->Body.Add(inner, {1, 0});
    definition->Body.Add(inner_ground, {0});
    Core::VCC supply;
    supply.SetName("V1");
    Core::Circuit unused;
    unused.Add(supply, {1});
    unused.AddSubcircuit(definition);
    // A ground in a block nobody uses grounds nothing
    CHECK_FALSE(unused.HasGround());
    CHECK(Core::CollectParts(unused).size() == 1);

    Core::SubcircuitInstance instance("load", {"IN"});
    instance.SetName("X1");
    Core::Circuit used;
    used.Add(supply, {1});
    used.Add(instance, {1});
    used.AddSubcircuit(definition);
    CHECK(used.HasGround());
    const std::vector<const Core::Component *> parts = Core::CollectParts(used);
    CHECK(parts.size() == 4);
    CHECK(std::ranges::contains(parts, &inner));
}

TEST_CASE("Picking another edge for a pin turns its marker in place, and the instances keep their wires",
          "[subcircuit]") {
    // Each edge has the rotation whose arrow comes from it
    for (const Core::PinSide side : GUI::PIN_SIDES) {
        const auto marker = GUI::CreateElement(Core::ComponentType::PIN, {0, 0}, GUI::GetSideRotation(side));
        CHECK(GUI::GetMarkerSide(*marker) == side);
    }

    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {8, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {-8, 0}, GUI::Rotation::R0));
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({-6, 0}, {-2, 0});
    schematic.SetSelection({0, 3, 4}, {});
    REQUIRE(schematic.CreateSubcircuitFromSelection("block", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    schematic.CommitUndoStep();

    auto content = schematic.OpenSubcircuit("block");
    REQUIRE(content);
    const std::vector<GUI::UIWire> inner_wires = (*content)->GetWires();
    // The input moves to the top of the block
    CHECK((*content)->SetPinMarkerSide({-2, 0}, Core::PinSide::TOP));
    CHECK_FALSE((*content)->SetPinMarkerSide({-2, 0}, Core::PinSide::TOP));
    CHECK_FALSE((*content)->SetPinMarkerSide({5, 5}, Core::PinSide::TOP));
    CHECK((*content)->IsModified());
    CHECK(GUI::FindMarkerSide((*content)->GetElements(), {-2, 0}) == Core::PinSide::TOP);
    // It turned about its terminal, so the wires inside are as they were
    CHECK((*content)->GetWires() == inner_wires);

    REQUIRE(schematic.UpdateSubcircuit("block", **content, {{"IN", {-2, 0}}, {"OUT", {2, 0}}}, {}));
    const GUI::UIElement &block = schematic.GetElement(2);
    CHECK(static_cast<const Core::SubcircuitInstance &>(block.GetComponent()).GetPinSide("IN") == Core::PinSide::TOP);
    const std::vector<GUI::GridPoint> terminals = block.GetTerminals();
    // The input terminal is above the box now, and the wire from the outside followed it
    CHECK(terminals[0].Y < block.GetPosition().Y);
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(nodes.GetNode({-6, 0}) == nodes.GetNode(terminals[0]));
    CHECK(nodes.GetNode({6, 0}) == nodes.GetNode(terminals[1]));
}
