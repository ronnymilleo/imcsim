/**
 * @file    live_input.h
 * @brief   How the user sets switches, push buttons, logic inputs and potentiometers on the schematic while a live
 *          transient runs.
 */

#ifndef IMCSIM_LIVE_INPUT_H
#define IMCSIM_LIVE_INPUT_H

#include "components/component.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

// Share of the track one notch of the mouse wheel moves the wiper of a potentiometer
inline constexpr double WIPER_STEP = 0.05;

/**
 * @enum    LiveAction
 * @brief   What the mouse does to a part during a live run.
 * @details A click flips a switch or a logic input; a push button is pressed while the mouse button holds it; the
 *          wheel turns a potentiometer.
 */
enum class LiveAction {
    NONE,
    FLIP,
    HOLD,
    TURN
};

// Parts
LiveAction GetLiveAction(Core::ComponentType type);
std::optional<std::size_t> PickLivePart(const std::vector<std::unique_ptr<UIElement>> &elements, ImVec2 world_pos);
const char *GetLiveEditNote(Core::ComponentType type);

// Changes
void Flip(Core::Component &component);
void SetPressed(Core::Component &component, bool is_pressed);
bool Turn(Core::Component &component, float wheel_notches);

} // namespace GUI

#endif // IMCSIM_LIVE_INPUT_H
