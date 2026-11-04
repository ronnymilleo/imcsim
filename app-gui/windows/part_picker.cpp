/**
 * @file    part_picker.cpp
 * @brief   The list of every part that Space opens over the editor, filtered by what is typed.
 */

#include "windows/part_picker.h"

#include "drawing/tool_icons.h"
#include "editing/element_factory.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace GUI {

namespace {

constexpr const char *PART_PICKER_POPUP = "Find a part";
/// How many matches the picker shows at once.
constexpr std::size_t PART_PICKER_ROWS = 5;
/// How far down the editor the top of the picker sits, as a share of it.
constexpr float PART_PICKER_TOP = 0.3f;
/// Width of the query and of the list, in font sizes.
constexpr float PART_PICKER_WIDTH = 16.0f;

} // namespace

/**
 * @brief   Creates the picker.
 * @param[in] preferences  The symbol standard of the part icons; they must outlive the picker.
 */
PartPicker::PartPicker(const Preferences &preferences) : m_Preferences(preferences) {
}

/**
 * @brief   Asks to open the picker on the next frame, as Find a Part in the context menu does.
 */
void PartPicker::RequestOpen() {
    m_IsOpenRequested = true;
}

/**
 * @brief   Opens the picker on Space or when asked, and draws it while it is open.
 * @param[in] blocks  The blocks of the document, listed after the parts, or none inside a block, since blocks do not
 *                    nest.
 * @return  The part or the block to place, on the frame it is picked, or no value.
 * @note    Call it inside the editor window, before anything that reads Esc: Esc only closes the list.
 */
std::optional<PlacingMode> PartPicker::Draw(const std::span<const SavedSubcircuit> blocks) {
    const bool space_pressed =
        ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (std::exchange(m_IsOpenRequested, false) || space_pressed) {
        m_Query.clear();
        m_Highlighted = 0;
        ImGui::OpenPopup(PART_PICKER_POPUP);
    }
    // Hangs from its top edge, a third of the way down the editor, so it stays put as the list filters
    const ImVec2 anchor = {ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2.0f,
                           ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * PART_PICKER_TOP};
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing, {0.5f, 0.0f});
    // Without navigation the arrow keys move the highlight below instead of taking focus from the query
    if (!ImGui::BeginPopup(PART_PICKER_POPUP, ImGuiWindowFlags_NoNav)) {
        return std::nullopt;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const float row_height = GetToolButtonSide();
    const float width = ImGui::GetFontSize() * PART_PICKER_WIDTH;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint("##query", "Find a part", &m_Query)) {
        m_Highlighted = 0;
    }
    const std::vector<Core::ComponentType> found = FindParts(m_Query);
    std::vector<const SavedSubcircuit *> definitions;
    for (const SavedSubcircuit &definition : blocks) {
        if (definition.Name.contains(m_Query)) {
            definitions.push_back(&definition);
        }
    }
    const std::size_t match_count = found.size() + definitions.size();
    // The list follows the highlight only when the keys move it, so the wheel still scrolls it
    bool moved = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_Highlighted + 1 < match_count) {
        ++m_Highlighted;
        moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_Highlighted > 0) {
        --m_Highlighted;
        moved = true;
    }
    m_Highlighted = std::min(m_Highlighted, match_count == 0 ? 0 : match_count - 1);

    std::optional<PlacingMode> picked;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (match_count > 0 && enter) {
        if (m_Highlighted < found.size()) {
            picked = PlacingMode{.Type = found[m_Highlighted]};
        } else {
            picked = PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT,
                                 .Block = definitions[m_Highlighted - found.size()]->Name};
        }
    }
    if (match_count == 0) {
        ImGui::TextDisabled("No part matches");
    }
    // A short list that scrolls, so the picker stays small
    const float row_spacing = ImGui::GetStyle().ItemSpacing.y;
    const std::size_t visible_rows = std::min(match_count, PART_PICKER_ROWS);
    const float list_height = static_cast<float>(visible_rows) * (row_height + row_spacing) - row_spacing;
    if (visible_rows > 0) {
        ImGui::BeginChild("##matches", {width + ImGui::GetStyle().ScrollbarSize, list_height}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoNav);
    }
    for (std::size_t index = 0; index < found.size(); ++index) {
        if (SelectablePartRow(found[index], index == m_Highlighted, width, m_Preferences.Symbols)) {
            picked = PlacingMode{.Type = found[index]};
        }
        if (moved && index == m_Highlighted) {
            ImGui::SetScrollHereY();
        }
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const std::size_t match = found.size() + index;
        ImGui::PushID(static_cast<int>(match));
        if (ImGui::Selectable(std::format("Block: {}", definitions[index]->Name).c_str(), match == m_Highlighted,
                              ImGuiSelectableFlags_None, {width, row_height})) {
            picked = PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = definitions[index]->Name};
        }
        ImGui::PopID();
        if (moved && match == m_Highlighted) {
            ImGui::SetScrollHereY();
        }
    }
    if (visible_rows > 0) {
        ImGui::EndChild();
    }

    if (picked) {
        ImGui::CloseCurrentPopup();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Esc only closes the list; the lock keeps the editor below from also leaving its mode this frame
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetCurrentWindow()->ID, ImGuiInputFlags_LockThisFrame);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return picked;
}

} // namespace GUI
