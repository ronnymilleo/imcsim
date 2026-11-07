/**
 * @file    part_picker.cpp
 * @brief   The part search of the command bar: a field whose list of matches hangs below it as the user types.
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

/// How many matches the list shows at once.
constexpr std::size_t PART_PICKER_ROWS = 6;
/// Width of the list, in font sizes.
constexpr float PART_PICKER_WIDTH = 16.0f;

} // namespace

/**
 * @brief   Creates the picker.
 * @param[in] preferences  The symbol standard of the part icons; they must outlive the picker.
 */
PartPicker::PartPicker(const Preferences &preferences) : m_Preferences(preferences) {
}

/**
 * @brief   Asks to focus the field on the next frame, as All parts on the rail and Find a part in the context menu do.
 */
void PartPicker::RequestOpen() {
    m_IsOpenRequested = true;
}

/**
 * @brief   Focuses the field on Space, while the calling window has the focus and no field of it is active.
 * @note    Call it inside the editor window, every frame it draws.
 */
void PartPicker::WatchShortcut() {
    if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        m_IsOpenRequested = true;
    }
}

/**
 * @brief   Draws the field at the cursor and, while it is in use, the list of what it finds below it.
 * @param[in] blocks  The blocks that can be placed where the editor is, listed after the parts.
 * @param[in] width   Width of the field, in pixels.
 * @return  The mode that places the part or the block picked, on the frame it is picked, or no value: placing for a
 *          part or a block of the document, pasting for a ready block, which brings its definition along.
 */
std::optional<EditorMode> PartPicker::Draw(const std::vector<const SavedSubcircuit *> &blocks, const float width) {
    if (std::exchange(m_IsOpenRequested, false)) {
        m_Query.clear();
        m_Highlighted = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(width);
    const bool is_entered = ImGui::InputTextWithHint("##part_search", "Find a part (Space)", &m_Query,
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        m_Highlighted = 0;
    }
    const bool is_active = ImGui::IsItemActive();
    const ImVec2 list_position = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
    if (ImGui::IsItemActivated()) {
        m_IsListShown = true;
    }
    if (!m_IsListShown) {
        return std::nullopt;
    }
    const PartMatches matches = FindMatches(m_Query, blocks);
    const bool moved = is_active && MoveHighlight(matches.GetCount());
    std::optional<EditorMode> picked;
    if (is_entered && matches.GetCount() > 0) {
        picked = matches.Get(m_Highlighted);
    }
    if (std::optional<EditorMode> clicked = DrawList(matches, list_position, moved)) {
        picked = std::move(clicked);
    }
    // The list stays while the field is in use or the mouse is on it, so a click on a match lands before it closes
    if (picked || ImGui::IsKeyPressed(ImGuiKey_Escape) || (!is_active && !m_IsListHovered)) {
        m_IsListShown = false;
    }
    if (picked) {
        m_Query.clear();
        m_IsEditorFocusRequested = true;
        ImGui::ClearActiveID();
    }
    return picked;
}

/**
 * @brief   Tells the editor to take the focus back after a pick, once.
 * @return  True on the first call after a part was picked.
 */
bool PartPicker::TakeEditorFocus() {
    return std::exchange(m_IsEditorFocusRequested, false);
}

/**
 * @brief   The list of matches in a window of its own below the field, over the windows under it, which never takes the
 *          focus from the field.
 * @details Returns the match clicked this frame, or none.
 */
std::optional<EditorMode> PartPicker::DrawList(const PartMatches &matches, const ImVec2 position, const bool moved) {
    ImGui::SetNextWindowPos(position);
    constexpr ImGuiWindowFlags LIST_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoDocking;
    std::optional<EditorMode> clicked;
    if (ImGui::Begin("##part_matches", nullptr, LIST_FLAGS)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        // The field is still the active item on the frame of the click, which must not hide the list under the mouse
        m_IsListHovered =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (matches.GetCount() == 0) {
            ImGui::TextDisabled("No part matches");
        } else {
            clicked = DrawMatches(matches, moved);
        }
    }
    ImGui::End();
    return clicked;
}

/**
 * @brief   How many parts and blocks match.
 */
std::size_t PartPicker::PartMatches::GetCount() const {
    return Parts.size() + ReadyBlocks.size() + Blocks.size();
}

/**
 * @brief   What placing the match at a position of the list means: a part, a ready block after the parts, or a block
 *          of the document after those; none for a ready block that cannot be read.
 */
std::optional<EditorMode> PartPicker::PartMatches::Get(const std::size_t index) const {
    if (index < Parts.size()) {
        return PlacingMode{.Type = Parts[index]};
    }
    if (index < Parts.size() + ReadyBlocks.size()) {
        auto group = MakeReadyBlock(*ReadyBlocks[index - Parts.size()]);
        if (!group) {
            return std::nullopt;
        }
        return PastingMode{.Group = std::move(*group)};
    }
    return PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT,
                       .Block = Blocks[index - Parts.size() - ReadyBlocks.size()]->Name};
}

/**
 * @brief   The parts and the ready blocks whose names hold the query, and the blocks of the document whose names hold
 *          it as typed.
 */
PartPicker::PartMatches PartPicker::FindMatches(const std::string &query,
                                                const std::vector<const SavedSubcircuit *> &blocks) {
    PartMatches matches{.Parts = FindParts(query), .ReadyBlocks = FindReadyBlocks(query), .Blocks = {}};
    for (const SavedSubcircuit *definition : blocks) {
        if (definition->Name.contains(query)) {
            matches.Blocks.push_back(definition);
        }
    }
    return matches;
}

/**
 * @brief   Up and Down move the highlight within the matches, which also keeps it inside a list that got shorter.
 * @details Returns whether the keys moved it, so the list follows the highlight only then and the wheel still scrolls
 *          it.
 */
bool PartPicker::MoveHighlight(const std::size_t match_count) {
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
    return moved;
}

/**
 * @brief   A short list of the matches that scrolls, so the picker stays small: the parts with their icons, then the
 *          ready blocks and the blocks of the document.
 * @details Returns the match clicked this frame, or none.
 */
std::optional<EditorMode> PartPicker::DrawMatches(const PartMatches &matches, const bool moved) {
    const float row_height = GetToolButtonSide();
    const float width = ImGui::GetFontSize() * PART_PICKER_WIDTH;
    const float row_spacing = ImGui::GetStyle().ItemSpacing.y;
    const std::size_t visible_rows = std::min(matches.GetCount(), PART_PICKER_ROWS);
    if (visible_rows == 0) {
        return std::nullopt;
    }
    const float list_height = static_cast<float>(visible_rows) * (row_height + row_spacing) - row_spacing;
    ImGui::BeginChild("##matches", {width + ImGui::GetStyle().ScrollbarSize, list_height}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoNav);
    std::optional<EditorMode> clicked;
    for (std::size_t index = 0; index < matches.GetCount(); ++index) {
        const bool is_highlighted = index == m_Highlighted;
        if (DrawMatchRow(matches, index, is_highlighted, width)) {
            clicked = matches.Get(index);
        }
        if (moved && is_highlighted) {
            ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();
    return clicked;
}

/**
 * @brief   One row of the list: a part with its icon, or a block by its name, a ready block telling what it does when
 *          hovered.
 * @details Returns whether it was clicked.
 */
bool PartPicker::DrawMatchRow(const PartMatches &matches, const std::size_t index, const bool is_highlighted,
                              const float width) {
    if (index < matches.Parts.size()) {
        return SelectablePartRow(matches.Parts[index], is_highlighted, width, m_Preferences.Symbols);
    }
    const std::size_t ready_index = index - matches.Parts.size();
    const ReadyBlock *ready = ready_index < matches.ReadyBlocks.size() ? matches.ReadyBlocks[ready_index] : nullptr;
    const std::string label =
        ready != nullptr ? std::format("Logic block: {}", ready->Title)
                         : std::format("Block: {}", matches.Blocks[ready_index - matches.ReadyBlocks.size()]->Name);
    ImGui::PushID(static_cast<int>(index));
    const bool is_clicked =
        ImGui::Selectable(label.c_str(), is_highlighted, ImGuiSelectableFlags_None, {width, GetToolButtonSide()});
    ImGui::PopID();
    if (ready != nullptr) {
        ImGui::SetItemTooltip("%.*s", static_cast<int>(ready->Description.size()), ready->Description.data());
    }
    return is_clicked;
}

} // namespace GUI
