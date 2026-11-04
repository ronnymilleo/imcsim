/**
 * @file    part_names_tests.cpp
 * @brief   Tests for renaming a part and keeping what refers to it by name on it.
 */

#include "components/controlled_source.h"
#include "document/schematic.h"
#include "editing/element_factory.h"
#include "editing/part_names.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

GUI::Schematic MakeTwoResistors() {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 4}, GUI::Rotation::R0));
    return schematic;
}

} // namespace

TEST_CASE("A new name keeps the SPICE letter of the part and is unique whatever its case", "[part_names]") {
    const GUI::Schematic schematic = MakeTwoResistors();
    const auto &elements = schematic.GetElements();
    CHECK_FALSE(GUI::CheckPartName(elements, 0, "RLOAD").has_value());
    CHECK_FALSE(GUI::CheckPartName(elements, 0, "R_top2").has_value());
    // The letter tells ngspice what the part is
    CHECK(GUI::CheckPartName(elements, 0, "LOAD").value_or("").contains("Start with R"));
    CHECK(GUI::CheckPartName(elements, 0, "R").has_value());
    CHECK(GUI::CheckPartName(elements, 0, "R-1").has_value());
    // R2 is taken, and SPICE reads r2 as the same name
    CHECK(GUI::CheckPartName(elements, 0, "R2").value_or("").contains("R2 already has this name"));
    // Its own name is no clash
    CHECK_FALSE(GUI::CheckPartName(elements, 0, "R1").has_value());

    GUI::Schematic with_ground;
    with_ground.AddElement(GUI::CreateElement(Core::ComponentType::GROUND, {0, 0}, GUI::Rotation::R0));
    CHECK(GUI::CheckPartName(with_ground.GetElements(), 0, "G1").has_value());
}

TEST_CASE("A current of a renamed part takes the new name and keeps its terminal", "[part_names]") {
    CHECK(GUI::RenameCurrent("R1", "R1", "RLOAD") == "RLOAD");
    CHECK(GUI::RenameCurrent("Q1.C", "Q1", "QOUT") == "QOUT.C");
    // R10 is another part, not a current of R1
    CHECK(GUI::RenameCurrent("R10", "R1", "RLOAD") == "R10");
    CHECK(GUI::RenameCurrent("D1", "R1", "RLOAD") == "D1");

    CHECK(GUI::RenameCurrentsIn("I(R1)*V(OUT)-I(R10)", "R1", "RLOAD") == "I(RLOAD)*V(OUT)-I(R10)");
    CHECK(GUI::RenameCurrentsIn("I(Q1.B)+I(Q1.C)", "Q1", "QOUT") == "I(QOUT.B)+I(QOUT.C)");
    CHECK(GUI::RenameCurrentsIn("V(R1)", "R1", "RLOAD") == "V(R1)");
    CHECK(GUI::RenameCurrentsIn("I(R1", "R1", "RLOAD") == "I(R1");
}

TEST_CASE("Renaming a part carries its measured current, the sources that follow it and the math", "[part_names]") {
    GUI::Schematic schematic = MakeTwoResistors();
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::CCCS, {8, 0}, GUI::Rotation::R0));
    auto &follower = static_cast<Core::ControlledSource &>(schematic.GetElement(2).GetComponent());
    follower.SetControllingCurrent("R1");
    schematic.SetCurrentMeasured("R1", true);
    schematic.SetCurrentMeasured("R2", true);
    schematic.SetMathChannels({{.Name = "M1", .Expression = "I(R1)*2"}});
    schematic.CommitUndoStep();

    REQUIRE(schematic.RenameElement(0, "RLOAD"));
    CHECK(schematic.GetElement(0).GetComponent().GetName() == "RLOAD");
    CHECK(follower.GetControllingCurrent() == "RLOAD");
    CHECK(schematic.IsCurrentMeasured("RLOAD"));
    CHECK_FALSE(schematic.IsCurrentMeasured("R1"));
    CHECK(schematic.IsCurrentMeasured("R2"));
    CHECK(schematic.GetMathChannels()[0].Expression == "I(RLOAD)*2");
    CHECK(schematic.IsModified());

    // A refused name changes nothing
    const auto refused = schematic.RenameElement(0, "rload2x");
    REQUIRE_FALSE(refused);
    CHECK(refused.error().contains("Start with R"));
    CHECK_FALSE(schematic.RenameElement(1, "Rload"));
    CHECK(schematic.GetElement(1).GetComponent().GetName() == "R2");

    // Undo brings the old name back
    schematic.CommitUndoStep();
    schematic.Undo();
    CHECK(schematic.GetElement(0).GetComponent().GetName() == "R1");
}
