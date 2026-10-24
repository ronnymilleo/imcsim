/**
 * @file    live_input_tests.cpp
 * @brief   Tests for how the user sets switches, push buttons, logic inputs and potentiometers on the schematic while
 *          a live transient runs.
 */

#include "live_input.h"

#include "components/logic_input.h"
#include "components/potentiometer.h"
#include "components/switch.h"
#include "element_factory.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("A click flips switches and logic inputs, holds push buttons and the wheel turns potentiometers",
          "[live_input]") {
    CHECK(GUI::GetLiveAction(Core::ComponentType::SWITCH) == GUI::LiveAction::FLIP);
    CHECK(GUI::GetLiveAction(Core::ComponentType::LOGIC_INPUT) == GUI::LiveAction::FLIP);
    CHECK(GUI::GetLiveAction(Core::ComponentType::PUSH_BUTTON) == GUI::LiveAction::HOLD);
    CHECK(GUI::GetLiveAction(Core::ComponentType::POTENTIOMETER) == GUI::LiveAction::TURN);
    CHECK(GUI::GetLiveAction(Core::ComponentType::RESISTOR) == GUI::LiveAction::NONE);
    CHECK(GUI::GetLiveAction(Core::ComponentType::V_SWITCH) == GUI::LiveAction::NONE);
}

TEST_CASE("The part picked is the topmost one under the cursor, only when the user can set it", "[live_input]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    elements.push_back(GUI::CreateElement(Core::ComponentType::SWITCH, {20, 0}, GUI::Rotation::R0));
    CHECK(GUI::PickLivePart(elements, {20.0f, 0.0f}) == 1);
    CHECK_FALSE(GUI::PickLivePart(elements, {0.0f, 0.0f}).has_value());
    CHECK_FALSE(GUI::PickLivePart(elements, {50.0f, 50.0f}).has_value());

    // A resistor drawn over the switch takes the click, as it would to select
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {20, 0}, GUI::Rotation::R0));
    CHECK_FALSE(GUI::PickLivePart(elements, {20.0f, 0.0f}).has_value());
}

TEST_CASE("Editing a part during a live run warns when the edit ends the run", "[live_input]") {
    // A switch has nothing to edit but the state the run takes
    CHECK(GUI::GetLiveEditNote(Core::ComponentType::SWITCH) == nullptr);
    CHECK(GUI::GetLiveEditNote(Core::ComponentType::PUSH_BUTTON) == nullptr);
    // These have a field the run takes and others that end it
    CHECK(std::string(GUI::GetLiveEditNote(Core::ComponentType::LOGIC_INPUT)).contains("logic level ends"));
    CHECK(std::string(GUI::GetLiveEditNote(Core::ComponentType::POTENTIOMETER)).contains("resistance ends"));
    CHECK(std::string(GUI::GetLiveEditNote(Core::ComponentType::RESISTOR)) == "Changing this part ends the live run");
}

TEST_CASE("Flipping toggles a switch and a logic input, and leaves a push button to be held", "[live_input]") {
    Core::Switch toggle(Core::ComponentType::SWITCH);
    GUI::Flip(toggle);
    CHECK(toggle.IsClosed());
    GUI::Flip(toggle);
    CHECK_FALSE(toggle.IsClosed());

    Core::LogicInput input;
    GUI::Flip(input);
    CHECK(input.IsHigh());

    Core::Switch button(Core::ComponentType::PUSH_BUTTON);
    GUI::Flip(button);
    CHECK_FALSE(button.IsClosed());
    GUI::SetPressed(button, true);
    CHECK(button.IsClosed());
    GUI::SetPressed(button, false);
    CHECK_FALSE(button.IsClosed());

    // A toggle switch is not pressed and released
    GUI::SetPressed(toggle, true);
    CHECK_FALSE(toggle.IsClosed());
}

TEST_CASE("Turning moves the wiper a step per notch and stops at the ends of the track", "[live_input]") {
    Core::Potentiometer pot;
    pot.SetWiper(0.5);
    CHECK(GUI::Turn(pot, 2.0f));
    CHECK(pot.GetWiper() == Catch::Approx(0.5 + 2.0 * GUI::WIPER_STEP));
    CHECK_FALSE(GUI::Turn(pot, 0.0f));
    CHECK(GUI::Turn(pot, -100.0f));
    CHECK(pot.GetWiper() == 0.0);
    CHECK_FALSE(GUI::Turn(pot, -1.0f));

    Core::Switch toggle(Core::ComponentType::SWITCH);
    CHECK_FALSE(GUI::Turn(toggle, 1.0f));
}
