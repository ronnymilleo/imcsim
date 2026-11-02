/**
 * @file    live_input.cpp
 * @brief   How the user sets switches, push buttons, logic inputs and potentiometers on the schematic while a live
 *          transient runs.
 */

#include "live_input.h"

#include "components/logic_input.h"
#include "components/potentiometer.h"
#include "components/switch.h"
#include <algorithm>

namespace GUI {

/**
 * @brief   Tells what the mouse does to a kind of part during a live run.
 * @param[in] type  Kind of component.
 * @return  Flip for a switch and a logic input, Hold for a push button, Turn for a potentiometer and None for the
 *          other parts.
 */
LiveAction GetLiveAction(const Core::ComponentType type) {
    switch (type) {
    case Core::ComponentType::SWITCH:
    case Core::ComponentType::LOGIC_INPUT:
        return LiveAction::FLIP;
    case Core::ComponentType::PUSH_BUTTON:
        return LiveAction::HOLD;
    case Core::ComponentType::POTENTIOMETER:
        return LiveAction::TURN;
    default:
        return LiveAction::NONE;
    }
}

/**
 * @brief   Finds the part under the cursor that the user can set during a live run.
 * @param[in] elements   Parts of the schematic.
 * @param[in] world_pos  Cursor position, in world units.
 * @return  The index of the topmost part under the cursor when it can be set, or no value when there is none or
 *          the topmost one cannot, so a part drawn over another takes the click as it would to select.
 */
std::optional<std::size_t> PickLivePart(const std::vector<std::unique_ptr<UIElement>> &elements,
                                        const ImVec2 world_pos) {
    for (std::size_t index = elements.size(); index-- > 0;) {
        if (elements[index]->Contains(world_pos)) {
            if (GetLiveAction(elements[index]->GetComponent().GetType()) == LiveAction::NONE) {
                return std::nullopt;
            }
            return index;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Tells what editing the properties of a part does to a live run, so a change does not end it by surprise.
 * @param[in] type  Kind of component.
 * @return  The note shown above the fields, or null for a switch or a push button, whose only field changes in the run.
 */
const char *GetLiveEditNote(const Core::ComponentType type) {
    switch (type) {
    case Core::ComponentType::SWITCH:
    case Core::ComponentType::PUSH_BUTTON:
        return nullptr;
    case Core::ComponentType::LOGIC_INPUT:
        return "High changes in the live run; changing the logic level ends it";
    case Core::ComponentType::POTENTIOMETER:
        return "The wiper changes in the live run; changing the resistance ends it";
    default:
        return "Changing this part ends the live run";
    }
}

/**
 * @brief   Opens a closed switch and closes an open one, or sets a logic input to the other level.
 * @param[in,out] component  A switch or a logic input; any other part is left as it is.
 */
void Flip(Core::Component &component) {
    if (component.GetType() == Core::ComponentType::SWITCH) {
        auto &contact = static_cast<Core::Switch &>(component);
        contact.SetClosed(!contact.IsClosed());
    } else if (component.GetType() == Core::ComponentType::LOGIC_INPUT) {
        auto &input = static_cast<Core::LogicInput &>(component);
        input.SetHigh(!input.IsHigh());
    }
}

/**
 * @brief   Presses or releases a push button.
 * @param[in,out] component   A push button; any other part is left as it is.
 * @param[in]     is_pressed  True while the mouse button holds it.
 */
void SetPressed(Core::Component &component, const bool is_pressed) {
    if (component.GetType() == Core::ComponentType::PUSH_BUTTON) {
        static_cast<Core::Switch &>(component).SetClosed(is_pressed);
    }
}

/**
 * @brief   Moves the wiper of a potentiometer by notches of the mouse wheel.
 * @param[in,out] component      A potentiometer; any other part is left as it is.
 * @param[in]     wheel_notches  Notches turned, positive toward terminal 2.
 * @return  True when the wiper moved; false at the end of the track, without notches or for another part.
 */
bool Turn(Core::Component &component, const float wheel_notches) {
    if (component.GetType() != Core::ComponentType::POTENTIOMETER || wheel_notches == 0.0f) {
        return false;
    }
    auto &pot = static_cast<Core::Potentiometer &>(component);
    const double wiper = std::clamp(pot.GetWiper() + static_cast<double>(wheel_notches) * WIPER_STEP, 0.0, 1.0);
    if (wiper == pot.GetWiper()) {
        return false;
    }
    pot.SetWiper(wiper);
    return true;
}

} // namespace GUI
