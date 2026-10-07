/**
 * @file    schematic_file_tests.cpp
 * @brief   Tests for saving schematics to JSON and loading them back.
 */

#include "element_factory.h"
#include "schematic_file.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

GUI::LoadedSchematic LoadOrFail(const std::string &text) {
    auto loaded = GUI::LoadSchematic(text);
    if (!loaded) {
        FAIL(loaded.error());
    }
    return std::move(*loaded);
}

std::string Document(const std::string &elements, const std::string &wires) {
    return R"({"format": "imcsim-schematic", "version": 1, "elements": [)" + elements + R"(], "wires": [)" + wires +
           "]}";
}

} // namespace

TEST_CASE("A saved schematic loads back unchanged", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {0, 0}, GUI::Rotation::R90));
    elements.back()->GetComponent().SetName("R1");
    elements.back()->GetComponent().SetValue(4.7e3);
    elements.push_back(GUI::CreateElement(Core::ComponentType::VCC, {0, -2}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("V1");
    elements.back()->GetComponent().SetValue(12.0);
    elements.push_back(GUI::CreateElement(Core::ComponentType::Ground, {0, 2}, GUI::Rotation::R0));
    const std::vector<GUI::UIWire> wires{{{0, 2}, {4, 2}}, {{4, 2}, {4, -2}}};

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, wires));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const GUI::UIElement &original = *elements[index];
        const GUI::UIElement &copy = *loaded.Elements[index];
        CHECK(copy.GetComponent().GetType() == original.GetComponent().GetType());
        CHECK(copy.GetComponent().GetName() == original.GetComponent().GetName());
        CHECK(copy.GetComponent().GetValue() == original.GetComponent().GetValue());
        CHECK(copy.GetPosition() == original.GetPosition());
        CHECK(copy.GetRotation() == original.GetRotation());
    }
    REQUIRE(loaded.Wires.size() == wires.size());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        CHECK(loaded.Wires[index].GetStart() == wires[index].GetStart());
        CHECK(loaded.Wires[index].GetEnd() == wires[index].GetEnd());
    }
}

TEST_CASE("Saved files store ground without name or value", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Ground, {1, 2}, GUI::Rotation::R0));
    const std::string text = GUI::SaveSchematic(elements, {});
    CHECK(text.find("\"name\"") == std::string::npos);
    CHECK(text.find("\"value\"") == std::string::npos);
}

TEST_CASE("Files that are not schematics this version can read fail to load", "[schematic_file]") {
    CHECK_FALSE(GUI::LoadSchematic("not json"));
    CHECK_FALSE(GUI::LoadSchematic("[1, 2]"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "something-else", "version": 1})"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "imcsim-schematic"})"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "imcsim-schematic", "version": 2})"));
}

TEST_CASE("Invalid wires are skipped with a warning and the rest still loads", "[schematic_file]") {
    const GUI::LoadedSchematic loaded =
        LoadOrFail(Document(R"({"type": "Resistor", "name": "R1", "value": 1000, "x": 0, "y": 0, "rotation": 0})",
                            R"({"start": [0, 0], "end": [3, 2]}, {"start": [1, 1], "end": [1, 1]}, {"start": [0, 0]},
           {"start": [2, 0], "end": [2, 5]})"));

    CHECK(loaded.Elements.size() == 1);
    REQUIRE(loaded.Wires.size() == 1);
    CHECK(loaded.Wires[0].GetEnd() == GUI::GridPoint{2, 5});
    CAPTURE(loaded.Warnings);
    REQUIRE(loaded.Warnings.size() == 3);
    CHECK(loaded.Warnings[0].find("diagonal") != std::string::npos);
    CHECK(loaded.Warnings[1].find("zero length") != std::string::npos);
}

TEST_CASE("Invalid elements are skipped with a warning", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Transistor", "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R1", "value": -5, "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R2", "value": 10, "x": 0, "y": 0, "rotation": 45},
           {"type": "Resistor", "name": "R3", "value": 10, "x": 1.5, "y": 0, "rotation": 0},
           {"type": "Capacitor", "name": "C1", "value": 1e-6, "x": 4, "y": 0, "rotation": 90})",
        ""));

    REQUIRE(loaded.Elements.size() == 1);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "C1");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 4);
}

TEST_CASE("Missing and repeated names are replaced with the next free number", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Resistor", "name": "R1", "value": 10, "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R1", "value": 10, "x": 4, "y": 0, "rotation": 0},
           {"type": "Resistor", "value": 10, "x": 8, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "C7", "value": 10, "x": 12, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "Rload", "value": 10, "x": 16, "y": 0, "rotation": 0})",
        ""));

    REQUIRE(loaded.Elements.size() == 5);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "R1");
    CHECK(loaded.Elements[1]->GetComponent().GetName() == "R2");
    CHECK(loaded.Elements[2]->GetComponent().GetName() == "R3");
    CHECK(loaded.Elements[3]->GetComponent().GetName() == "R4");
    CHECK(loaded.Elements[4]->GetComponent().GetName() == "Rload");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 3);
}
