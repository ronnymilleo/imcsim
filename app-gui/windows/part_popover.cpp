/**
 * @file    part_popover.cpp
 * @brief   The popover at the top of the editor canvas that holds the properties of the selected part.
 */

#include "windows/part_popover.h"

#include "imgui_internal.h"
#include <optional>
#include <utility>

namespace GUI {

namespace {

constexpr const char *PART_POPOVER = "Part properties";
/// How far below the top of the canvas the properties popover opens, in font sizes.
constexpr float POPOVER_TOP_MARGIN_FONT_SIZES = 1.0f;

/**
 * @brief   The character of a key that starts a value, from the main row or the keypad: a digit, a point or a minus.
 */
std::optional<char> FindTypedValueStart() {
    for (int digit = 0; digit <= 9; ++digit) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
            ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false)) {
            return static_cast<char>('0' + digit);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) {
        return '.';
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
        return '-';
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Tells where the canvas is, so the popover opens centered across it.
 * @param[in] center  The middle of the canvas on screen.
 * @param[in] top     The top edge of the canvas on screen.
 */
void PartPopover::SetCanvas(const ImVec2 center, const float top) {
    m_CanvasCenter = center;
    m_CanvasTop = top;
}

/**
 * @brief   Sets where the popover opens: at the top of the canvas, centered across it, so it fits whatever part asked
 *          for it and has the height of the canvas to grow into when a part turns custom.
 */
void PartPopover::Place() {
    m_Anchor = {m_CanvasCenter.x, m_CanvasTop + ImGui::GetFontSize() * POPOVER_TOP_MARGIN_FONT_SIZES};
}

/**
 * @brief   Opens the popover on the next Draw().
 */
void PartPopover::Request() {
    m_IsRequested = true;
}

/**
 * @brief   Places the popover and opens it on the next Draw().
 */
void PartPopover::Open() {
    Place();
    Request();
}

/**
 * @brief   A digit, a point or a minus typed on a selected part starts editing its value, as in LTspice: "4k7" and
 *          Enter.
 * @details Keys are read rather than characters, since SDL sends characters only while a text field is active.
 * @param[in,out] part_editor  Fields of the selected part, which start taking the typed value.
 */
void PartPopover::HandleValueTyping(PartEditor &part_editor) {
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsWindowFocused() || ImGui::IsAnyItemActive() || io.KeyCtrl || io.KeyAlt || io.KeyShift ||
        !part_editor.CanTypeValue()) {
        return;
    }
    const std::optional<char> first = FindTypedValueStart();
    if (!first) {
        return;
    }
    part_editor.StartTypingValue(*first);
    Open();
}

/**
 * @brief   Draws the properties of the selected part in a popover at the top of the canvas. Edits apply as they
 *          are typed; Enter or Esc closes it, and so does a click outside.
 * @param[in,out] part_editor  Fields of the selected part.
 * @param[in]     schematic    Schematic on show, whose selection the popover follows.
 */
void PartPopover::Draw(PartEditor &part_editor, Schematic &schematic) {
    if (std::exchange(m_IsRequested, false)) {
        ImGui::OpenPopup(PART_POPOVER);
    }
    ImGui::SetNextWindowPos(m_Anchor, ImGuiCond_Appearing, {0.5f, 0.0f});
    if (!ImGui::BeginPopup(PART_POPOVER)) {
        return;
    }
    // Undo or a deletion may take the part away while the popover is open
    if (schematic.GetSelectedElement() == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    part_editor.Draw();
    // The Enter that opened it is ignored; Esc closes only the popover, not the selection below
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && !ImGui::IsWindowAppearing()) {
        ImGui::CloseCurrentPopup();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace GUI
