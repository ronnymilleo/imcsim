/**
 * @file    ui_element_tests.cpp
 * @brief   Tests for terminal positions and picking of schematic elements.
 */

#include "test_printers.h"
#include "ui_elements/ui_bjt.h"
#include "ui_elements/ui_diode.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_mosfet.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_vcc.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <vector>

TEST_CASE("Two-terminal elements have terminals two units each side, after rotation", "[ui_element]") {
    GUI::UIResistor resistor({5, 5}, GUI::Rotation::R0);
    CHECK(resistor.GetTerminals() == std::vector<GUI::GridPoint>{{3, 5}, {7, 5}});

    resistor.SetRotation(GUI::Rotation::R90);
    CHECK(resistor.GetTerminals() == std::vector<GUI::GridPoint>{{5, 3}, {5, 7}});

    resistor.SetPosition({0, 0});
    CHECK(resistor.GetTerminals() == std::vector<GUI::GridPoint>{{0, -2}, {0, 2}});
}

TEST_CASE("Ground and VCC have a single terminal at their position", "[ui_element]") {
    const GUI::UIGround ground({2, 3}, GUI::Rotation::R180);
    CHECK(ground.GetTerminals() == std::vector<GUI::GridPoint>{{2, 3}});

    const GUI::UIVCC supply({-1, 4}, GUI::Rotation::R90);
    CHECK(supply.GetTerminals() == std::vector<GUI::GridPoint>{{-1, 4}});
}

TEST_CASE("Elements own a component of their type", "[ui_element]") {
    const GUI::UIResistor resistor({0, 0}, GUI::Rotation::R0);
    CHECK(resistor.GetComponent().GetType() == Core::ComponentType::Resistor);

    const GUI::UIGround ground({0, 0}, GUI::Rotation::R0);
    CHECK(ground.GetComponent().GetType() == Core::ComponentType::Ground);
}

TEST_CASE("Contains follows the element rotation", "[ui_element]") {
    GUI::UIResistor resistor({0, 0}, GUI::Rotation::R0);
    CHECK(resistor.Contains({0.0f, 0.0f}));
    CHECK(resistor.Contains({1.9f, 0.5f}));
    CHECK_FALSE(resistor.Contains({0.0f, 1.5f}));

    resistor.SetRotation(GUI::Rotation::R90);
    CHECK(resistor.Contains({0.5f, 1.9f}));
    CHECK_FALSE(resistor.Contains({1.9f, 0.0f}));
}

TEST_CASE("Ground is picked below its terminal", "[ui_element]") {
    const GUI::UIGround ground({0, 0}, GUI::Rotation::R0);
    CHECK(ground.Contains({0.0f, 1.2f}));
    CHECK_FALSE(ground.Contains({0.0f, -0.5f}));
}

TEST_CASE("Diode parts run from the anode at x = -2 to the cathode at x = +2", "[ui_element]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::Diode, Core::ComponentType::ZenerDiode, Core::ComponentType::LED}) {
        const GUI::UIDiode diode(type, {1, 1}, GUI::Rotation::R180);
        CHECK(diode.GetComponent().GetType() == type);
        CHECK(diode.GetTerminals() == std::vector<GUI::GridPoint>{{3, 1}, {-1, 1}});
    }
}

TEST_CASE("An LED is also picked on its arrows", "[ui_element]") {
    const GUI::UIDiode diode(Core::ComponentType::Diode, {0, 0}, GUI::Rotation::R0);
    const GUI::UIDiode led(Core::ComponentType::LED, {0, 0}, GUI::Rotation::R0);
    CHECK_FALSE(diode.Contains({0.3f, -1.0f}));
    CHECK(led.Contains({0.3f, -1.0f}));
}

TEST_CASE("Transistors have collector or drain, base or gate, and emitter or source terminals", "[ui_element]") {
    GUI::UIBJT bjt(Core::ComponentType::NPN, {0, 0}, GUI::Rotation::R0);
    CHECK(bjt.GetTerminals() == std::vector<GUI::GridPoint>{{1, -2}, {-2, 0}, {1, 2}});
    bjt.SetRotation(GUI::Rotation::R90);
    for (const GUI::GridPoint terminal : bjt.GetTerminals()) {
        // A quarter turn keeps every terminal on the grid, two units from the body at most
        CHECK(std::abs(terminal.X) <= 2);
        CHECK(std::abs(terminal.Y) <= 2);
    }

    const GUI::UIMOSFET mosfet(Core::ComponentType::PMOS, {3, 3}, GUI::Rotation::R180);
    CHECK(mosfet.GetComponent().GetType() == Core::ComponentType::PMOS);
    CHECK(mosfet.GetTerminals() == std::vector<GUI::GridPoint>{{2, 5}, {5, 3}, {2, 1}});
}

TEST_CASE("Transistors are picked inside their body but not beyond the terminals", "[ui_element]") {
    const GUI::UIMOSFET mosfet(Core::ComponentType::NMOS, {0, 0}, GUI::Rotation::R0);
    CHECK(mosfet.Contains({0.0f, 0.0f}));
    CHECK(mosfet.Contains({1.0f, -1.9f}));
    CHECK_FALSE(mosfet.Contains({2.0f, 0.0f}));
    CHECK_FALSE(mosfet.Contains({0.0f, 2.5f}));
}

TEST_CASE("Mirroring flips an element across its local vertical axis before rotating it", "[ui_element]") {
    GUI::UIBJT transistor(Core::ComponentType::PNP, {0, 0}, GUI::Rotation::R0);
    transistor.SetMirrored(true);
    CHECK(transistor.IsMirrored());
    // Collector, base and emitter: the base moves to the right
    CHECK(transistor.GetTerminals() == std::vector<GUI::GridPoint>{{-1, -2}, {2, 0}, {-1, 2}});
    // Mirrored and turned half a turn, the emitter ends on top, as a high-side PNP is drawn
    transistor.SetRotation(GUI::Rotation::R180);
    CHECK(transistor.GetTerminals() == std::vector<GUI::GridPoint>{{1, 2}, {-2, 0}, {1, -2}});

    GUI::UIResistor resistor({5, 5}, GUI::Rotation::R0);
    resistor.SetMirrored(true);
    CHECK(resistor.GetTerminals() == std::vector<GUI::GridPoint>{{7, 5}, {3, 5}});
}

TEST_CASE("Picking follows the mirrored outline", "[ui_element]") {
    GUI::UIMOSFET mosfet(Core::ComponentType::NMOS, {0, 0}, GUI::Rotation::R0);
    // The gate side reaches x = -2 and the drain and source side only x = 1.2
    CHECK(mosfet.Contains({-1.5f, 0.0f}));
    CHECK_FALSE(mosfet.Contains({1.5f, 0.0f}));
    mosfet.SetMirrored(true);
    CHECK(mosfet.Contains({1.5f, 0.0f}));
    CHECK_FALSE(mosfet.Contains({-1.5f, 0.0f}));
}

TEST_CASE("The inward direction of a terminal follows its lead into the part", "[ui_element]") {
    GUI::UIResistor resistor({0, 0}, GUI::Rotation::R0);
    CHECK(resistor.GetTerminalInward(0) == GUI::GridPoint{1, 0});
    CHECK(resistor.GetTerminalInward(1) == GUI::GridPoint{-1, 0});
    // A quarter turn puts the first terminal at the top, so its lead goes down into the part
    resistor.SetRotation(GUI::Rotation::R90);
    CHECK(resistor.GetTerminalInward(0) == GUI::GridPoint{0, 1});
    resistor.SetMirrored(true);
    CHECK(resistor.GetTerminalInward(0) == GUI::GridPoint{0, -1});

    // Collector and emitter leads are vertical even though their terminals sit right of the middle
    const GUI::UIBJT bjt(Core::ComponentType::NPN, {0, 0}, GUI::Rotation::R0);
    CHECK(bjt.GetTerminalInward(0) == GUI::GridPoint{0, 1});
    CHECK(bjt.GetTerminalInward(1) == GUI::GridPoint{1, 0});
    CHECK(bjt.GetTerminalInward(2) == GUI::GridPoint{0, -1});

    // The supply terminal is at its middle, and its lead goes up to the bar
    const GUI::UIVCC supply({0, 0}, GUI::Rotation::R180);
    CHECK(supply.GetTerminalInward(0) == GUI::GridPoint{0, 1});
}
