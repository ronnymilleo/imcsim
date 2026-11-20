/**
 * @file    clipboard_actions_tests.cpp
 * @brief   Tests for copy, cut and paste of the editor through the clipboard of the system.
 */

#include "schematic/clipboard.h"
#include "schematic/element_factory.h"
#include "schematic/schematic.h"
#include "test_printers.h"
#include "windows/clipboard_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

namespace {

/**
 * @brief   A resistor and a capacitor side by side, with a wire between them.
 */
GUI::Schematic TwoPartsAndAWire() {
    GUI::Schematic schematic;
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    schematic.AddElement(GUI::CreateElement(Core::ComponentType::CAPACITOR, {10, 0}, GUI::Rotation::R0));
    schematic.AddWire({2, 0}, {8, 0});
    return schematic;
}

} // namespace

TEST_CASE("Nothing selected writes no text, so the clipboard keeps what it had", "[clipboard_actions]") {
    const GUI::Schematic schematic = TwoPartsAndAWire();
    CHECK_FALSE(GUI::WriteSelectionText(schematic).has_value());
}

TEST_CASE("The selection is written as text that pastes back as the selected parts only", "[clipboard_actions]") {
    GUI::Schematic schematic = TwoPartsAndAWire();
    schematic.SetSelection({.Elements = {1}, .Wires = {}});

    const std::optional<std::string> text = GUI::WriteSelectionText(schematic);
    REQUIRE(text);
    const auto group = GUI::ParseCopiedSchematic(*text);
    REQUIRE(group);
    REQUIRE(group->Elements.size() == 1);
    CHECK(group->Elements[0]->GetComponent().GetType() == Core::ComponentType::CAPACITOR);
    CHECK(group->Wires.empty());
}
