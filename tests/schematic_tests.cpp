/**
 * @file    schematic_tests.cpp
 * @brief   Tests for the shared schematic document: content, names, selection and modified state.
 */

#include "element_factory.h"
#include "schematic.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<GUI::UIElement> MakeResistor(const GUI::GridPoint position) {
    return GUI::CreateElement(Core::ComponentType::Resistor, position, GUI::Rotation::R0);
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
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::Ground, {0, 2}, GUI::Rotation::R0));

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
    CHECK(schematic.GetSelectedWireIndex() == 0);
    CHECK(schematic.GetSelectionVersion() != element_version);
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
    CHECK_FALSE(schematic.GetSelectedElementIndex().has_value());
    CHECK_FALSE(schematic.GetSelectedWireIndex().has_value());
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

TEST_CASE("SimplifyAllWires deselects a wire because indices change", "[schematic]") {
    GUI::Schematic schematic;
    schematic.AddWire({0, 0}, {3, 0});
    schematic.AddWire({3, 0}, {6, 0});
    schematic.SelectWire(1);

    schematic.SimplifyAllWires();
    CHECK(schematic.GetWires().size() == 1);
    CHECK_FALSE(schematic.GetSelectedWireIndex().has_value());
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
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::Ground, {2, 0}, GUI::Rotation::R0));
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
