/**
 * @file    live_input_handler.cpp
 * @brief   What the mouse does to the parts the user sets by hand during a live run: flip, hold, turn.
 */

#include "windows/live_input_handler.h"

#include "imgui.h"
#include "schematic/live_input.h"
#include <utility>

namespace GUI {

/**
 * @brief   The part under the cursor that the user sets by hand during a live run, or none outside one or outside
 *          selecting.
 * @param[in] schematic              Schematic on show.
 * @param[in] view                   Pan and zoom of the canvas.
 * @param[in] is_live_part_pickable  Whether parts can be set by hand now: a live run goes on, on the document, while
 *                                   the editor is selecting.
 */
std::optional<std::size_t> LiveInputHandler::FindLivePart(const Schematic &schematic, const ViewTransform &view,
                                                          const bool is_live_part_pickable) const {
    if (!is_live_part_pickable) {
        return std::nullopt;
    }
    return PickLivePart(schematic.GetElements(), view.ToWorld(ImGui::GetIO().MousePos));
}

/**
 * @brief   A click flips a switch or a logic input, a push button stays pressed while the mouse button holds it, and
 *          the wheel turns a potentiometer; each change goes to the run through the schematic.
 * @param[in,out] schematic  Schematic on show, whose part changes.
 * @param[in]     live_part  The part under the cursor, from FindLivePart().
 * @return  Whether the mouse went to a part, so the click does not also select.
 */
bool LiveInputHandler::HandleInput(Schematic &schematic, const std::optional<std::size_t> live_part) {
    if (m_HeldButton) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ReleaseHeldButton(schematic);
        }
        return true;
    }
    if (!live_part) {
        return false;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    Core::Component &component = schematic.GetElement(*live_part).GetComponent();
    switch (GetLiveAction(component.GetType())) {
    case LiveAction::TURN:
        if (Turn(component, ImGui::GetIO().MouseWheel)) {
            schematic.MarkLiveChanged(component);
        }
        return false;
    case LiveAction::FLIP:
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return false;
        }
        Flip(component);
        schematic.MarkLiveChanged(component);
        return true;
    case LiveAction::HOLD:
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return false;
        }
        SetPressed(component, true);
        schematic.MarkLiveChanged(component);
        m_HeldButton = live_part;
        return true;
    case LiveAction::NONE:
    default:
        return false;
    }
}

/**
 * @brief   The push button the mouse holds down, or none.
 */
std::optional<std::size_t> LiveInputHandler::GetHeldButton() const {
    return m_HeldButton;
}

/**
 * @brief   The push button lets go when the mouse button does, wherever the cursor is then.
 */
void LiveInputHandler::ReleaseHeldButton(Schematic &schematic) {
    const std::optional<std::size_t> index = std::exchange(m_HeldButton, std::nullopt);
    if (!index || *index >= schematic.GetElements().size()) {
        return;
    }
    Core::Component &component = schematic.GetElement(*index).GetComponent();
    SetPressed(component, false);
    schematic.MarkLiveChanged(component);
}

} // namespace GUI
