/**
 * @file    schematic_tests.cpp
 * @brief   Tests for the shared schematic document: content, names, selection and modified state.
 */

#include "components/net_label.h"
#include "components/switch.h"
#include "element_factory.h"
#include "examples.h"
#include "schematic.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

std::unique_ptr<GUI::UIElement> MakeResistor(const GUI::GridPoint position) {
    return GUI::CreateElement(Core::ComponentType::RESISTOR, position, GUI::Rotation::R0);
}

} // namespace

TEST_CASE("A new schematic is empty, untitled and unmodified", "[schematic]") {
    const GUI::Schematic schematic;
    CHECK(schematic.GetElements().empty());
    CHECK(schematic.GetWires().empty());
    CHECK_FALSE(schematic.GetFilePath().has_value());
    CHECK_FALSE(schematic.IsModified());
}

TEST_CASE("Added elements get the next free name and mark the schematic as modified", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(MakeResistor({4, 0}));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {0, 2}, GUI::Rotation::R0));

    REQUIRE(schematic.GetElements().size() == 3);
    CHECK(schematic.GetElements()[0]->GetComponent().GetName() == "R1");
    CHECK(schematic.GetElements()[1]->GetComponent().GetName() == "R2");
    CHECK(schematic.GetElements()[2]->GetComponent().GetName().empty());
    CHECK(schematic.IsModified());
}

TEST_CASE("Names keep counting after a deletion instead of reusing a number", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(MakeResistor({4, 0}));
    schematic.SelectElement(0);
    schematic.DeleteSelection();
    schematic.AddElement(MakeResistor({8, 0}));

    REQUIRE(schematic.GetElements().size() == 2);
    CHECK(schematic.GetElements()[1]->GetComponent().GetName() == "R3");
}

TEST_CASE("AddWire ignores zero-length segments", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({1, 1}, {1, 1});
    CHECK(schematic.GetWires().empty());
    CHECK_FALSE(schematic.IsModified());

    schematic.AddWire({0, 0}, {3, 0});
    CHECK(schematic.GetWires().size() == 1);
    CHECK(schematic.IsModified());
}

TEST_CASE("Selecting replaces the previous selection and bumps the version", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddWire({2, 0}, {2, 4});

    const std::size_t initial_version = schematic.GetSelectionVersion();
    schematic.SelectElement(0);
    CHECK(schematic.GetSelectedElement() == schematic.GetElements()[0].get());
    CHECK(schematic.GetSelectionVersion() != initial_version);

    const std::size_t element_version = schematic.GetSelectionVersion();
    schematic.SelectWire(0);
    CHECK(schematic.GetSelectedElement() == nullptr);
    CHECK(schematic.IsWireSelected(0));
    CHECK_FALSE(schematic.IsElementSelected(0));
    CHECK(schematic.GetSelectionVersion() != element_version);
}

TEST_CASE("The selection holds several elements and wires, and one element is edited only when alone", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(MakeResistor({8, 0}));
    schematic.AddWire({2, 0}, {6, 0});
    CHECK_FALSE(schematic.HasSelection());

    schematic.SetSelection({0, 1}, {0});
    CHECK(schematic.HasSelection());
    CHECK(schematic.GetSelectedElements() == std::set<std::size_t>{0, 1});
    CHECK(schematic.GetSelectedWires() == std::set<std::size_t>{0});
    CHECK(schematic.GetSelectedElement() == nullptr);

    schematic.SetSelection({1}, {});
    CHECK(schematic.GetSelectedElement() == schematic.GetElements()[1].get());
    // A wire selected beside the element makes it a group, which has no single part to edit
    schematic.SetSelection({1}, {0});
    CHECK(schematic.GetSelectedElement() == nullptr);
}

TEST_CASE("DeleteSelection removes the selected item and clears the selection", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddWire({2, 0}, {2, 4});

    SECTION("an element, leaving its wires in place") {
        schematic.SelectElement(0);
        schematic.DeleteSelection();
        CHECK(schematic.GetElements().empty());
        CHECK(schematic.GetWires().size() == 1);
    }
    SECTION("a wire") {
        schematic.SelectWire(0);
        schematic.DeleteSelection();
        CHECK(schematic.GetElements().size() == 1);
        CHECK(schematic.GetWires().empty());
    }
    CHECK_FALSE(schematic.HasSelection());
}

TEST_CASE("DeleteSelection removes every selected element and wire in one step", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(MakeResistor({8, 0}));
    schematic.AddElement(MakeResistor({16, 0}));
    schematic.AddWire({2, 0}, {6, 0});
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({20, 0}, {24, 0});
    schematic.CommitUndoStep();

    schematic.SetSelection({0, 2}, {0, 2});
    schematic.DeleteSelection();
    REQUIRE(schematic.GetElements().size() == 1);
    CHECK(schematic.GetElements()[0]->GetPosition() == GUI::GridPoint{8, 0});
    REQUIRE(schematic.GetWires().size() == 1);
    CHECK(schematic.GetWires()[0].GetStart() == GUI::GridPoint{10, 0});
    CHECK_FALSE(schematic.HasSelection());

    schematic.CommitUndoStep();
    schematic.Undo();
    CHECK(schematic.GetElements().size() == 3);
    CHECK(schematic.GetWires().size() == 3);
}

TEST_CASE("SetWires invalidates the nodes without marking the schematic as modified", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({0, 0}, {4, 0});
    schematic.MarkSaved("circuit.imcsim");
    CHECK_FALSE(schematic.GetConnectivity().GetNode({0, 4}).has_value());

    schematic.SetWires({{{0, 0}, {0, 4}}});
    CHECK_FALSE(schematic.IsModified());
    CHECK(schematic.GetConnectivity().GetNode({0, 4}).has_value());
}

TEST_CASE("SimplifyAllWires deselects the wires because indices change, and keeps the elements", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 8}));
    schematic.AddWire({0, 0}, {3, 0});
    schematic.AddWire({3, 0}, {6, 0});
    schematic.SetSelection({0}, {1});

    schematic.SimplifyAllWires();
    CHECK(schematic.GetWires().size() == 1);
    CHECK(schematic.GetSelectedWires().empty());
    CHECK(schematic.IsElementSelected(0));
}

TEST_CASE("MarkSaved records the file and clears the modified state", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.MarkSaved("circuit.imcsim");
    CHECK_FALSE(schematic.IsModified());
    CHECK(schematic.GetFilePath() == std::filesystem::path("circuit.imcsim"));
}

TEST_CASE("Clear empties the schematic and forgets its file", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.MarkSaved("circuit.imcsim");
    schematic.SelectElement(0);

    schematic.Clear();
    CHECK(schematic.GetElements().empty());
    CHECK_FALSE(schematic.GetFilePath().has_value());
    CHECK_FALSE(schematic.IsModified());
    CHECK(schematic.GetSelectedElement() == nullptr);
}

TEST_CASE("Replace swaps the content but keeps the file", "[schematic]") {
    GUI::Schematic schematic;
    schematic.MarkSaved("circuit.imcsim");
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(MakeResistor({0, 0}));

    schematic.Replace(std::move(elements), {{{2, 0}, {2, 3}}}, {});
    CHECK(schematic.GetElements().size() == 1);
    CHECK(schematic.GetWires().size() == 1);
    CHECK(schematic.GetFilePath().has_value());
    CHECK_FALSE(schematic.IsModified());
}

TEST_CASE("BuildSpiceNetlist reflects value changes made through MarkModified", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {2, 0}, GUI::Rotation::R0));
    CHECK(schematic.BuildSpiceNetlist().find("R1 1 0 1k") != std::string::npos);

    schematic.GetElement(0).GetComponent().SetValue(4.7e3);
    schematic.MarkModified();
    CHECK(schematic.BuildSpiceNetlist().find("R1 1 0 4.7k") != std::string::npos);
}

TEST_CASE("Simulation results are dropped when the circuit changes", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({0, 0}, {4, 0});
    schematic.SetOperatingPoint(Core::OperatingPoint{{0.0, 5.0}});
    REQUIRE(schematic.GetOperatingPoint());

    SECTION("but not when the wires are set to what they already are, as during a drag that did not move") {
        schematic.SetWires(schematic.GetWires());
        schematic.SimplifyAllWires();
        CHECK(schematic.GetOperatingPoint());
    }
    SECTION("when the wires really change") {
        schematic.SetWires({{{0, 0}, {0, 4}}});
        CHECK_FALSE(schematic.GetOperatingPoint());
    }
    SECTION("when a value or anything else is edited") {
        schematic.SetTransient(Core::Transient{});
        schematic.SetACSweep(Core::ACSweep{});
        schematic.MarkModified();
        CHECK_FALSE(schematic.GetOperatingPoint());
        CHECK_FALSE(schematic.GetTransient());
        CHECK_FALSE(schematic.GetACSweep());
    }
}

TEST_CASE("Selecting does not drop simulation results", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.SetOperatingPoint(Core::OperatingPoint{{0.0, 5.0}});
    schematic.SelectElement(0);
    schematic.ClearSelection();
    CHECK(schematic.GetOperatingPoint());
}

TEST_CASE("Each analysis has its own results version", "[schematic]") {
    GUI::Schematic schematic;
    const std::size_t transient_version = schematic.GetTransientVersion();
    const std::size_t sweep_version = schematic.GetACSweepVersion();

    schematic.SetTransient(Core::Transient{});
    CHECK(schematic.GetTransientVersion() != transient_version);
    CHECK(schematic.GetACSweepVersion() == sweep_version);

    schematic.SetACSweep(Core::ACSweep{});
    schematic.SetOperatingPoint(Core::OperatingPoint{});
    CHECK(schematic.GetACSweepVersion() != sweep_version);
}

TEST_CASE("Undo and redo step through committed changes", "[schematic]") {
    GUI::Schematic schematic;
    CHECK_FALSE(schematic.CanUndo());
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.CommitUndoStep();
    schematic.AddWire({2, 0}, {6, 0});
    schematic.CommitUndoStep();

    schematic.Undo();
    CHECK(schematic.GetElements().size() == 1);
    CHECK(schematic.GetWires().empty());
    schematic.Undo();
    CHECK(schematic.GetElements().empty());
    CHECK_FALSE(schematic.CanUndo());

    schematic.Redo();
    schematic.Redo();
    CHECK(schematic.GetElements().size() == 1);
    CHECK(schematic.GetWires().size() == 1);
    CHECK_FALSE(schematic.CanRedo());
}

TEST_CASE("Undo restores values and positions edited in place", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.CommitUndoStep();

    // A drag moves the element several times before the gesture ends, and counts as one step
    schematic.GetElement(0).SetPosition({2, 0});
    schematic.MarkModified();
    schematic.GetElement(0).SetPosition({4, 0});
    schematic.MarkModified();
    schematic.CommitUndoStep();
    schematic.GetElement(0).GetComponent().SetValue(4.7e3);
    schematic.MarkModified();
    schematic.CommitUndoStep();

    schematic.Undo();
    CHECK(schematic.GetElement(0).GetComponent().GetValue() == 1e3);
    CHECK(schematic.GetElement(0).GetPosition() == GUI::GridPoint{4, 0});
    schematic.Undo();
    CHECK(schematic.GetElement(0).GetPosition() == GUI::GridPoint{0, 0});
    CHECK(schematic.GetElement(0).GetComponent().GetName() == "R1");
}

TEST_CASE("A new change after undoing drops what could be redone", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.CommitUndoStep();
    schematic.Undo();
    REQUIRE(schematic.CanRedo());

    schematic.AddElement(MakeResistor({4, 0}));
    schematic.CommitUndoStep();
    CHECK_FALSE(schematic.CanRedo());
}

TEST_CASE("Undo also takes back changes that were not committed yet", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    CHECK(schematic.CanUndo());
    schematic.Undo();
    CHECK(schematic.GetElements().empty());
}

TEST_CASE("Changes that leave the schematic as it was add no undo step", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.CommitUndoStep();
    schematic.Undo();
    schematic.Redo();

    schematic.GetElement(0).SetPosition({2, 0});
    schematic.GetElement(0).SetPosition({0, 0});
    schematic.MarkModified();
    schematic.CommitUndoStep();
    CHECK(schematic.CanUndo());
    schematic.Undo();
    CHECK(schematic.GetElements().empty());
}

TEST_CASE("Undoing back to the saved state makes the schematic unmodified again", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.MarkSaved("saved.imcsim");
    schematic.AddElement(MakeResistor({4, 0}));
    schematic.CommitUndoStep();
    REQUIRE(schematic.IsModified());

    schematic.Undo();
    CHECK_FALSE(schematic.IsModified());
    schematic.Undo();
    CHECK(schematic.IsModified());
    schematic.Redo();
    CHECK_FALSE(schematic.IsModified());
}

TEST_CASE("Opening or clearing a schematic starts a new history", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.CommitUndoStep();
    schematic.Clear();
    CHECK_FALSE(schematic.CanUndo());
    CHECK_FALSE(schematic.CanRedo());
}

TEST_CASE("The history keeps the last 100 steps", "[schematic]") {
    GUI::Schematic schematic;
    for (int index = 0; index < 105; ++index) {
        schematic.AddWire({index, 0}, {index, 1});
        schematic.CommitUndoStep();
    }
    int undone = 0;
    while (schematic.CanUndo()) {
        schematic.Undo();
        ++undone;
    }
    CHECK(undone == 100);
    CHECK(schematic.GetWires().size() == 5);
}

TEST_CASE("Changing the simulation settings is an unsaved change that keeps the results", "[schematic]") {
    GUI::Schematic schematic;
    schematic.SetOperatingPoint(Core::OperatingPoint{{0.0}});
    const std::size_t version = schematic.GetSimulationSettingsVersion();

    GUI::SimulationSettings settings;
    settings.Transient.StopTime = 1.0;
    schematic.SetSimulationSettings(settings);
    CHECK(schematic.IsModified());
    CHECK(schematic.GetSimulationSettings() == settings);
    CHECK(schematic.GetOperatingPoint());
    CHECK(schematic.GetSimulationSettingsVersion() == version);
}

TEST_CASE("Undo restores the simulation settings and changes their version", "[schematic]") {
    GUI::Schematic schematic;
    GUI::SimulationSettings settings;
    settings.StepSource = true;
    schematic.SetSimulationSettings(settings);
    schematic.CommitUndoStep();
    const std::size_t version = schematic.GetSimulationSettingsVersion();

    schematic.Undo();
    CHECK(schematic.GetSimulationSettings() == GUI::SimulationSettings{});
    CHECK(schematic.GetSimulationSettingsVersion() != version);
    CHECK_FALSE(schematic.IsModified());
}

TEST_CASE("Replace and Clear swap the simulation settings and change their version", "[schematic]") {
    GUI::Schematic schematic;
    GUI::SimulationSettings settings;
    settings.ACSweep.PointsPerDecade = 5;
    const std::size_t version = schematic.GetSimulationSettingsVersion();

    schematic.Replace({}, {}, settings);
    CHECK(schematic.GetSimulationSettings() == settings);
    CHECK(schematic.GetSimulationSettingsVersion() != version);
    CHECK_FALSE(schematic.IsModified());

    schematic.Clear();
    CHECK(schematic.GetSimulationSettings() == GUI::SimulationSettings{});
}

TEST_CASE("Measurements are saved by node point and restored by node", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {2, 0}, GUI::Rotation::R0));
    schematic.AddWire({-2, 0}, {-2, 4});
    const int node = *schematic.GetConnectivity().GetNode({-2, 4});
    schematic.SetVoltageMeasured(node, true);
    schematic.SetCurrentMeasured("R1", true);

    const GUI::SavedMeasurements saved = schematic.SaveMeasurements();
    REQUIRE(saved.Voltages.size() == 1);
    CHECK(schematic.GetConnectivity().GetNode(saved.Voltages[0]) == node);
    CHECK(saved.Currents == std::vector<std::string>{"R1"});

    schematic.ClearMeasurements();
    // Ground and points that connect to nothing are skipped
    schematic.RestoreMeasurements({.Voltages = {saved.Voltages[0], {2, 0}, {50, 50}}, .Currents = saved.Currents});
    CHECK(schematic.IsVoltageMeasured(node));
    CHECK_FALSE(schematic.IsVoltageMeasured(0));
    CHECK(schematic.IsCurrentMeasured("R1"));
}

TEST_CASE("Measuring is not an unsaved change and adds no undo step", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(MakeResistor({0, 0}));
    schematic.MarkSaved("circuit.imcsim");
    schematic.SetCurrentMeasured("R1", true);
    schematic.CommitUndoStep();
    CHECK_FALSE(schematic.IsModified());
    schematic.Undo();
    CHECK(schematic.GetElements().empty());
    CHECK(schematic.IsCurrentMeasured("R1"));
}

TEST_CASE("A measured node stays the same node when an edit renumbers the nodes", "[schematic]") {
    GUI::Schematic schematic;
    // Nodes are numbered by position, so the wire at x = 10 is node 1 until something appears to its left
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({20, 0}, {24, 0});
    const int measured = schematic.GetConnectivity().GetNode({20, 0}).value();
    schematic.SetVoltageMeasured(measured, true);

    schematic.AddWire({0, 0}, {4, 0});
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(nodes.GetNode({20, 0}).value() != measured);
    CHECK(schematic.IsVoltageMeasured(nodes.GetNode({20, 0}).value()));
    CHECK_FALSE(schematic.IsVoltageMeasured(nodes.GetNode({0, 0}).value()));
    CHECK_FALSE(schematic.IsVoltageMeasured(nodes.GetNode({10, 0}).value()));
}

TEST_CASE("A measured node survives changes that pile up before the nodes are asked for again", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({20, 0}, {24, 0});
    schematic.SetVoltageMeasured(schematic.GetConnectivity().GetNode({20, 0}).value(), true);

    schematic.AddWire({0, 0}, {4, 0});
    schematic.AddWire({5, 6}, {9, 6});
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(schematic.IsVoltageMeasured(nodes.GetNode({20, 0}).value()));
    CHECK(schematic.GetConnectivity().GetNode({10, 0}).value() != nodes.GetNode({20, 0}).value());
}

TEST_CASE("A measured node that loses all its wires is no longer measured", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({20, 0}, {24, 0});
    schematic.SetVoltageMeasured(schematic.GetConnectivity().GetNode({10, 0}).value(), true);

    // The other wire becomes node 1, the number the deleted wire had, but it was not the one measured
    schematic.SelectWire(0);
    schematic.DeleteSelection();
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK_FALSE(schematic.IsVoltageMeasured(nodes.GetNode({20, 0}).value()));
}

TEST_CASE("A measured node follows its wires when part of them moves", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({14, 0}, {14, 6});
    schematic.SetVoltageMeasured(schematic.GetConnectivity().GetNode({10, 0}).value(), true);

    // The horizontal piece slides away; the other piece still carries the node
    schematic.SetWires({{{10, 2}, {14, 2}}, {{14, 2}, {14, 6}}});
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(schematic.IsVoltageMeasured(nodes.GetNode({14, 6}).value()));
}

TEST_CASE("A measured node stays measured when a net label takes the place of a deleted wire", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({10, 0}, {14, 0});
    schematic.AddWire({14, 0}, {14, 6});
    schematic.AddWire({14, 6}, {20, 6});
    for (const GUI::GridPoint point : {GUI::GridPoint{10, 0}, GUI::GridPoint{20, 6}}) {
        schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, point, GUI::Rotation::R0));
        static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet("OUT");
    }
    schematic.SetVoltageMeasured(schematic.GetConnectivity().GetNode({10, 0}).value(), true);

    // The middle wire goes: the two halves are one node only through their labels
    schematic.SelectWire(1);
    schematic.DeleteSelection();
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    REQUIRE(nodes.GetNode({10, 0}) == nodes.GetNode({20, 6}));
    CHECK(schematic.IsVoltageMeasured(nodes.GetNode({10, 0}).value()));
}

TEST_CASE("A measured node survives net labels being placed, named and moved", "[schematic]") {
    const auto example = std::ranges::find_if(GUI::GetExamples(), [](const GUI::Example &candidate) {
        return std::string_view(candidate.Title).contains("Full-bridge");
    });
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded);
    GUI::Schematic schematic;
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings));
    schematic.RestoreMeasurements(loaded->Measurements);
    const auto measured_count = [&schematic] {
        int count = 0;
        const int nodes = 20;
        for (int node = 1; node < nodes; ++node) {
            count += schematic.IsVoltageMeasured(node) ? 1 : 0;
        }
        return count;
    };
    REQUIRE(measured_count() == 1);
    schematic.GetConnectivity();

    const GUI::GridPoint rail = schematic.GetWires().front().GetStart();
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, rail, GUI::Rotation::R0));
    const std::size_t first = schematic.GetElements().size() - 1;
    schematic.GetConnectivity();
    CHECK(measured_count() == 1);

    static_cast<Core::NetLabel &>(schematic.GetElement(first).GetComponent()).SetNet("OUT");
    schematic.MarkModified();
    schematic.GetConnectivity();
    CHECK(measured_count() == 1);

    schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, {60, 60}, GUI::Rotation::R0));
    const std::size_t second = schematic.GetElements().size() - 1;
    static_cast<Core::NetLabel &>(schematic.GetElement(second).GetComponent()).SetNet("OUT");
    schematic.MarkModified();
    schematic.GetConnectivity();
    CHECK(measured_count() == 1);

    for (const GUI::GridPoint position : {GUI::GridPoint{50, 50}, GUI::GridPoint{40, 40}, GUI::GridPoint{20, 20}}) {
        schematic.GetElement(second).SetPosition(position);
        schematic.MarkModified();
        schematic.GetConnectivity();
        CHECK(measured_count() == 1);
    }
}

TEST_CASE("A measured node that is joined to ground and then freed is measured again", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {0, 0}, GUI::Rotation::R0));
    schematic.AddWire({10, 10}, {14, 10});
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, {14, 10}, GUI::Rotation::R0));
    static_cast<Core::NetLabel &>(schematic.GetElements().back()->GetComponent()).SetNet("OUT");
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::NET_LABEL, {30, 30}, GUI::Rotation::R0));
    const std::size_t second = schematic.GetElements().size() - 1;
    static_cast<Core::NetLabel &>(schematic.GetElement(second).GetComponent()).SetNet("OUT");
    schematic.SetVoltageMeasured(schematic.GetConnectivity().GetNode({10, 10}).value(), true);

    // The second label lands on the terminal of the ground: the whole net is ground, which cannot be measured
    schematic.GetElement(second).SetPosition({0, 0});
    schematic.MarkModified();
    CHECK(schematic.GetConnectivity().GetNode({10, 10}) == 0);

    // Moved away, the net is a node again and its probe comes back
    schematic.GetElement(second).SetPosition({30, 30});
    schematic.MarkModified();
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(nodes.GetNode({10, 10}).value() > 0);
    CHECK(schematic.IsVoltageMeasured(nodes.GetNode({10, 10}).value()));
}

TEST_CASE("Chunks of a transient make the result grow, and only the first counts as a new result", "[schematic]") {
    GUI::Schematic schematic;
    const auto make_chunk = [](const std::vector<double> &times) {
        Core::Transient chunk;
        chunk.Times = times;
        chunk.NodeVoltages = {std::vector<double>(times.size(), 0.0), times};
        chunk.Currents = {{"R1", times}};
        return chunk;
    };

    schematic.AppendTransient(make_chunk({0.0, 1.0}));
    REQUIRE(schematic.GetTransient().has_value());
    // A transient that is still being made is not complete
    CHECK_FALSE(schematic.GetTransient()->IsComplete);
    const std::size_t result_version = schematic.GetTransientVersion();
    const std::size_t data_version = schematic.GetTransientDataVersion();

    schematic.AppendTransient(make_chunk({2.0, 3.0}));
    CHECK(schematic.GetTransient()->Times == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(schematic.GetTransient()->NodeVoltages[1] == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(schematic.GetTransient()->Currents[0].Values == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(schematic.GetTransientVersion() == result_version);
    CHECK(schematic.GetTransientDataVersion() == data_version + 1);

    // A chunk made for another circuit, with other nodes or other currents, is dropped
    Core::Transient other = make_chunk({4.0});
    other.NodeVoltages.push_back({4.0});
    schematic.AppendTransient(other);
    other = make_chunk({4.0});
    other.Currents[0].Name = "R2";
    schematic.AppendTransient(other);
    CHECK(schematic.GetTransient()->Times.size() == 4);
    CHECK(schematic.GetTransientDataVersion() == data_version + 1);

    // The whole run replaces what the chunks made, as a new result
    schematic.SetTransient(make_chunk({0.0, 5.0}));
    CHECK(schematic.GetTransient()->IsComplete);
    CHECK(schematic.GetTransientVersion() == result_version + 1);
    CHECK(schematic.GetTransientDataVersion() == data_version + 2);
}

TEST_CASE("A part set by hand during a live run keeps the transient and goes to the run as a change", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::SWITCH, {0, 0}, GUI::Rotation::R0));
    auto &contact = static_cast<Core::Switch &>(schematic.GetElement(0).GetComponent());
    schematic.SetTransient(Core::Transient{.Times = {0.0}, .NodeVoltages = {}, .Currents = {}, .Warnings = {}});
    schematic.SetOperatingPoint(Core::OperatingPoint{});
    schematic.CommitUndoStep();
    const std::size_t circuit_version = schematic.GetCircuitVersion();

    schematic.SetLiveRun(true);
    CHECK(schematic.IsLiveRun());
    contact.SetClosed(true);
    schematic.MarkLiveChanged(contact);
    // The transient that the run is making stays, the operating point no longer matches the circuit
    CHECK(schematic.GetTransient().has_value());
    CHECK_FALSE(schematic.GetOperatingPoint().has_value());
    CHECK(schematic.GetCircuitVersion() == circuit_version);
    CHECK(schematic.IsModified());
    CHECK(schematic.TakeLiveChanges() == Core::DescribeLiveChange(contact));
    CHECK(schematic.TakeLiveChanges().empty());

    // It is an undo step like any other, and undoing it is an edit that stops the run
    schematic.CommitUndoStep();
    schematic.Undo();
    CHECK_FALSE(static_cast<Core::Switch &>(schematic.GetElement(0).GetComponent()).IsClosed());
    CHECK(schematic.GetCircuitVersion() != circuit_version);
}

TEST_CASE("Outside a live run a part set by hand is an edit that drops the results", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::SWITCH, {0, 0}, GUI::Rotation::R0));
    auto &contact = static_cast<Core::Switch &>(schematic.GetElement(0).GetComponent());
    schematic.SetTransient(Core::Transient{});
    const std::size_t circuit_version = schematic.GetCircuitVersion();

    contact.SetClosed(true);
    schematic.MarkLiveChanged(contact);
    CHECK_FALSE(schematic.GetTransient().has_value());
    CHECK(schematic.GetCircuitVersion() != circuit_version);
    CHECK(schematic.TakeLiveChanges().empty());

    // The changes a run did not take are dropped when it ends
    schematic.SetLiveRun(true);
    schematic.MarkLiveChanged(contact);
    schematic.SetLiveRun(false);
    CHECK_FALSE(schematic.IsLiveRun());
    CHECK(schematic.TakeLiveChanges().empty());
}
