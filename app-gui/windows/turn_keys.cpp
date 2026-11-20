/**
 * @file    turn_keys.cpp
 * @brief   The keys that turn what the editor holds: R a quarter turn, M a mirror left to right, Shift+M top to bottom.
 */

#include "windows/turn_keys.h"

#include "imgui.h"

namespace GUI {

/**
 * @brief   What the turning keys of a frame ask for, the same in the placing, pasting and selecting modes.
 * @param[in] keys  The keys pressed and held this frame.
 * @return  A quarter turn for R, a mirror for M, left to right or, with Shift, top to bottom; none without either.
 *          R wins when both are pressed in the same frame.
 */
std::optional<PointTransform::Kind> GetTurnKind(const TurnKeys keys) {
    if (keys.IsRotatePressed) {
        return PointTransform::Kind::QUARTER_TURN;
    }
    if (keys.IsMirrorPressed) {
        return keys.IsShiftHeld ? PointTransform::Kind::MIRROR_TOP_BOTTOM : PointTransform::Kind::MIRROR_LEFT_RIGHT;
    }
    return std::nullopt;
}

/**
 * @brief   Reads the turning keys of this frame, as GetTurnKind() decodes them.
 * @return  The turn they ask for, or none.
 * @note    Read it only while the editor has the focus, so typing elsewhere does not turn anything.
 */
std::optional<PointTransform::Kind> ReadTurnKey() {
    return GetTurnKind({.IsRotatePressed = ImGui::IsKeyPressed(ImGuiKey_R),
                        .IsMirrorPressed = ImGui::IsKeyPressed(ImGuiKey_M),
                        .IsShiftHeld = ImGui::GetIO().KeyShift});
}

} // namespace GUI
