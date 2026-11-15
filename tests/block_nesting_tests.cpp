/**
 * @file    block_nesting_tests.cpp
 * @brief   Tests for blocks inside blocks: which blocks contain which, and how making, editing, saving, copying and
 *          simulating carry the nesting.
 */

#include "circuit/components/ground.h"
#include "circuit/components/net_label.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/vcc.h"
#include "circuit/subcircuit.h"
#include "schematic/block_nesting.h"
#include "schematic/clipboard.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file.h"
#include "schematic/subcircuit_pins.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

/**
 * @brief   A definition whose parts are one instance of each block named, enough for the questions of nesting.
 */
GUI::SavedSubcircuit MakeDefinition(const std::string &name, const std::vector<std::string> &placed) {
    GUI::SavedSubcircuit definition;
    definition.Name = name;
    for (const std::string &inner : placed) {
        auto element = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0);
        static_cast<Core::SubcircuitInstance &>(element->GetComponent()).SetDefinitionName(inner);
        definition.Elements.push_back(std::move(element));
    }
    return definition;
}

/**
 * @brief   A pin marker; unturned, its arrow points right, which puts the pin on the left of the block.
 */
void AddMarker(GUI::Schematic &schematic, const GUI::GridPoint point, const std::string &name,
               const GUI::Rotation rotation = GUI::Rotation::R0) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::PIN, point, rotation));
    static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet(name);
}

/**
 * @brief   Selects every part and wire, so the whole schematic becomes the next block.
 */
void SelectEverything(GUI::Schematic &schematic) {
    std::set<std::size_t> elements;
    for (std::size_t index = 0; index < schematic.GetElements().size(); ++index) {
        elements.insert(index);
    }
    std::set<std::size_t> wires;
    for (std::size_t index = 0; index < schematic.GetWires().size(); ++index) {
        wires.insert(index);
    }
    schematic.SetSelection(std::move(elements), std::move(wires));
}

/**
 * @brief   A resistor of the given value between IN and OUT markers turned into the block "inner", then that block with
 *          IN and OUT markers on its wires turned into the block "outer"; the schematic ends with one outer instance.
 */
void MakeNestedBlocks(GUI::Schematic &schematic, const double resistance = 1e3) {
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.GetElement(0).GetComponent().SetValue(resistance);
    AddMarker(schematic, {-2, 0}, "IN");
    AddMarker(schematic, {2, 0}, "OUT", GUI::Rotation::R180);
    SelectEverything(schematic);
    REQUIRE(schematic.CreateSubcircuitFromSelection("inner", {{"IN", {-2, 0}}, {"OUT", {2, 0}}}));
    // The inner instance is wired to where its markers were, so new markers there become the outer pins
    AddMarker(schematic, {-2, 0}, "A");
    AddMarker(schematic, {2, 0}, "B", GUI::Rotation::R180);
    SelectEverything(schematic);
    REQUIRE(schematic.GetSubcircuitPinCandidates());
    REQUIRE(schematic.CreateSubcircuitFromSelection("outer", {{"A", {-2, 0}}, {"B", {2, 0}}}));
}

const GUI::SavedSubcircuit &FindDefinition(const std::vector<GUI::SavedSubcircuit> &definitions,
                                           const std::string &name) {
    const auto found = std::ranges::find(definitions, name, &GUI::SavedSubcircuit::Name);
    REQUIRE(found != definitions.end());
    return *found;
}

/**
 * @brief   The block instances among some parts.
 */
std::vector<const Core::SubcircuitInstance *> ListInstances(const std::vector<std::unique_ptr<GUI::UIElement>> &parts) {
    std::vector<const Core::SubcircuitInstance *> instances;
    for (const auto &element : parts) {
        if (element->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
            instances.push_back(&static_cast<const Core::SubcircuitInstance &>(element->GetComponent()));
        }
    }
    return instances;
}

} // namespace

TEST_CASE("A block contains the blocks it places, at any depth, and itself", "[block_nesting]") {
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(MakeDefinition("top", {"middle", "leaf"}));
    definitions.push_back(MakeDefinition("middle", {"leaf"}));
    definitions.push_back(MakeDefinition("leaf", {}));

    CHECK(GUI::ListUsedBlocks(definitions[0].Elements) == std::set<std::string>{"leaf", "middle"});
    CHECK(GUI::ContainsBlock(definitions, "top", "leaf"));
    CHECK(GUI::ContainsBlock(definitions, "middle", "leaf"));
    CHECK(GUI::ContainsBlock(definitions, "leaf", "leaf"));
    CHECK_FALSE(GUI::ContainsBlock(definitions, "leaf", "top"));
    CHECK_FALSE(GUI::ContainsBlock(definitions, "middle", "top"));
    CHECK(GUI::UsesBlock(definitions[1].Elements, definitions, "leaf"));
    CHECK_FALSE(GUI::UsesBlock(definitions[1].Elements, definitions, "top"));
}

TEST_CASE("Blocks are ordered after the blocks they place, keeping the list order otherwise", "[block_nesting]") {
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(MakeDefinition("top", {"middle"}));
    definitions.push_back(MakeDefinition("alone", {}));
    definitions.push_back(MakeDefinition("middle", {"leaf"}));
    definitions.push_back(MakeDefinition("leaf", {}));
    // leaf, middle and top in that order; alone keeps its place after top
    CHECK(GUI::OrderByUse(definitions) == std::vector<std::size_t>{3, 2, 0, 1});
}

TEST_CASE("A cycle in a damaged file neither loops nor loses a block", "[block_nesting]") {
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(MakeDefinition("first", {"second"}));
    definitions.push_back(MakeDefinition("second", {"first"}));
    CHECK(GUI::ContainsBlock(definitions, "first", "first"));
    CHECK(GUI::ContainsBlock(definitions, "first", "second"));
    CHECK(GUI::OrderByUse(definitions).size() == 2);
}

TEST_CASE("Renaming a block points only its own instances at the new name", "[block_nesting]") {
    GUI::SavedSubcircuit definition = MakeDefinition("top", {"leaf", "other"});
    GUI::RenameBlockInstances(definition.Elements, "leaf", "leaf_2");
    CHECK(GUI::ListUsedBlocks(definition.Elements) == std::set<std::string>{"leaf_2", "other"});
}

TEST_CASE("A selection that holds a block becomes a block that places it", "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);

    REQUIRE(schematic.GetSubcircuits().size() == 2);
    const GUI::SavedSubcircuit &outer = FindDefinition(schematic.GetSubcircuits(), "outer");
    const auto inside = ListInstances(outer.Elements);
    REQUIRE(inside.size() == 1);
    CHECK(inside[0]->GetDefinitionName() == "inner");
    CHECK(inside[0]->GetPins() == std::vector<std::string>{"IN", "OUT"});
    const auto placed = ListInstances(schematic.GetElements());
    REQUIRE(placed.size() == 1);
    CHECK(placed[0]->GetDefinitionName() == "outer");
}

TEST_CASE("The netlist writes each block after the blocks it places, and the outer one places the inner",
          "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    const std::string netlist = schematic.BuildSpiceNetlist();
    const std::size_t inner = netlist.find(".subckt inner");
    const std::size_t outer = netlist.find(".subckt outer");
    REQUIRE(inner != std::string::npos);
    REQUIRE(outer != std::string::npos);
    CHECK(inner < outer);
    const std::string outer_body = netlist.substr(outer, netlist.find(".ends outer") - outer);
    CHECK(outer_body.contains(" inner\n"));
}

TEST_CASE("Opening a block gives its contents the other blocks, and its own blocks open in turn", "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    auto outer = schematic.OpenSubcircuit("outer");
    REQUIRE(outer);
    // The block itself is left out, so it cannot be placed inside itself
    REQUIRE((*outer)->GetSubcircuits().size() == 1);
    CHECK((*outer)->GetSubcircuits()[0].Name == "inner");
    const auto instances = ListInstances((*outer)->GetElements());
    REQUIRE(instances.size() == 1);
    CHECK(instances[0]->GetDefinitionName() == "inner");

    auto inner = schematic.OpenSubcircuit("inner");
    REQUIRE(inner);
    CHECK(std::ranges::any_of((*inner)->GetElements(), [](const auto &element) {
        return element->GetComponent().GetType() == Core::ComponentType::RESISTOR;
    }));
}

TEST_CASE("A block that would contain itself, directly or through another, is refused", "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    auto inner = schematic.OpenSubcircuit("inner");
    REQUIRE(inner);
    auto loop = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {20, 0}, GUI::Rotation::R0);
    GUI::ApplyDefinitionPins(static_cast<Core::SubcircuitInstance &>(loop->GetComponent()),
                             FindDefinition(schematic.GetSubcircuits(), "outer"));
    (*inner)->AddElement(std::move(loop));

    const auto updated =
        schematic.UpdateSubcircuit("inner", **inner, FindDefinition(schematic.GetSubcircuits(), "inner").Pins, {});
    REQUIRE_FALSE(updated);
    CHECK(updated.error().contains("itself"));
    CHECK(ListInstances(FindDefinition(schematic.GetSubcircuits(), "inner").Elements).empty());
}

TEST_CASE("A renamed pin of an inner block reaches its instances inside the outer block", "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    auto inner = schematic.OpenSubcircuit("inner");
    REQUIRE(inner);
    REQUIRE(schematic.UpdateSubcircuit("inner", **inner, {{"FROM", {-2, 0}}, {"OUT", {2, 0}}}, {}));

    const auto instances = ListInstances(FindDefinition(schematic.GetSubcircuits(), "outer").Elements);
    REQUIRE(instances.size() == 1);
    CHECK(instances[0]->GetPins() == std::vector<std::string>{"FROM", "OUT"});
}

TEST_CASE("Nested blocks round-trip through a file whatever order it lists them in", "[block_nesting]") {
    GUI::Schematic schematic;
    MakeNestedBlocks(schematic);
    auto loaded = GUI::LoadSchematic(
        GUI::SaveSchematic(schematic.GetElements(), schematic.GetWires(), {}, {}, schematic.GetSubcircuits()));
    REQUIRE(loaded);
    // The outer block first, before the block it places
    std::ranges::reverse(loaded->Subcircuits);
    auto reversed =
        GUI::LoadSchematic(GUI::SaveSchematic(loaded->Elements, loaded->Wires, {}, {}, loaded->Subcircuits));
    REQUIRE(reversed);
    CHECK(reversed->Warnings.empty());
    REQUIRE(reversed->Subcircuits.size() == 2);
    CHECK(reversed->Subcircuits[0].Name == "outer");
    const auto instances = ListInstances(reversed->Subcircuits[0].Elements);
    REQUIRE(instances.size() == 1);
    CHECK(instances[0]->GetPins() == std::vector<std::string>{"IN", "OUT"});
}

TEST_CASE("Copying a block carries the blocks inside it, and a paste renames a clashing inner block everywhere",
          "[block_nesting]") {
    GUI::Schematic source;
    MakeNestedBlocks(source);
    const std::size_t outer_instance = source.GetElements().size() - 1;
    const std::string text = GUI::CopySelection(source.GetElements(), source.GetWires(),
                                                {.Elements = {outer_instance}, .Wires = {}}, source.GetSubcircuits());
    auto group = GUI::ParseCopiedSchematic(text);
    REQUIRE(group);
    REQUIRE(group->Subcircuits.size() == 2);

    // The target has its own, different, inner block
    GUI::Schematic target;
    MakeNestedBlocks(target, 2e3);
    std::vector<GUI::SavedSubcircuit> definitions = std::move(group->Subcircuits);
    std::ranges::reverse(definitions);
    const std::vector<std::string> names = target.MergeSubcircuits(std::move(definitions), group->Elements);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "outer_2");
    CHECK(names[1] == "inner_2");
    const auto inside = ListInstances(FindDefinition(target.GetSubcircuits(), "outer_2").Elements);
    REQUIRE(inside.size() == 1);
    CHECK(inside[0]->GetDefinitionName() == "inner_2");
    const auto pasted = ListInstances(group->Elements);
    REQUIRE(pasted.size() == 1);
    CHECK(pasted[0]->GetDefinitionName() == "outer_2");
}

TEST_CASE("The checks look into blocks inside blocks and refuse one that contains itself", "[block_nesting]") {
    Core::Resistor resistor;
    resistor.SetName("R1");
    const Core::Ground ground;
    auto leaf = std::make_shared<Core::SubcircuitDefinition>();
    leaf->Name = "leaf";
    leaf->Pins = {{"IN", 1}};
    leaf->Body.Add(resistor, {1, 0});
    leaf->Body.Add(ground, {0});
    Core::SubcircuitInstance inner("leaf", {"IN"});
    inner.SetName("X1");
    auto middle = std::make_shared<Core::SubcircuitDefinition>();
    middle->Name = "middle";
    middle->Pins = {{"IN", 1}};
    middle->Body.Add(inner, {1});
    Core::SubcircuitInstance outer("middle", {"IN"});
    outer.SetName("X1");
    Core::Circuit circuit;
    circuit.Add(outer, {1});
    circuit.AddSubcircuit(middle);
    circuit.AddSubcircuit(leaf);
    // The ground two levels down counts
    CHECK(circuit.HasGround());

    Core::SubcircuitInstance looped("middle", {"IN"});
    looped.SetName("X2");
    middle->Body.Add(looped, {1});
    const std::optional<std::string> problem = Core::CheckCircuit(circuit);
    REQUIRE(problem);
    CHECK(problem->contains("itself"));
}

TEST_CASE("ngspice runs a divider two blocks down, with the outer block passing a parameter on", "[block_nesting]") {
    // leaf is a divider whose lower resistor follows RBOTTOM; middle places it and sets RBOTTOM from its own LOW
    Core::Resistor upper;
    upper.SetName("R1");
    upper.SetValue(1e3);
    Core::Resistor lower;
    lower.SetName("R2");
    lower.SetValueExpression("{RBOTTOM}");
    auto leaf = std::make_shared<Core::SubcircuitDefinition>();
    leaf->Name = "leaf";
    leaf->Pins = {{"IN", 1}, {"OUT", 2}};
    leaf->Parameters = {{"RBOTTOM", 1e3}};
    leaf->Body.Add(upper, {1, 2});
    leaf->Body.Add(lower, {2, 0});
    Core::SubcircuitInstance inner("leaf", {"IN", "OUT"});
    inner.SetName("X1");
    inner.SetParameter("RBOTTOM", 3e3);
    auto middle = std::make_shared<Core::SubcircuitDefinition>();
    middle->Name = "middle";
    middle->Pins = {{"IN", 1}, {"OUT", 2}};
    middle->Body.Add(inner, {1, 2});
    Core::SubcircuitInstance outer("middle", {"IN", "OUT"});
    outer.SetName("X1");
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(8.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(outer, {1, 2});
    circuit.Add(ground, {0});
    // Listed outer first: the netlist puts leaf before middle anyway
    circuit.AddSubcircuit(middle);
    circuit.AddSubcircuit(leaf);

    REQUIRE_FALSE(Core::CheckCircuit(circuit));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    REQUIRE(run.Result->NodeVoltages.size() == 3);
    CHECK(run.Result->NodeVoltages[2] == Catch::Approx(8.0 * 3.0 / 4.0).margin(1e-6));
}
