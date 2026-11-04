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
 * @param[in] blocks  The blocks that can be placed where the editor is, listed after the parts.
 * @return  The part or the block to place, on the frame it is picked, or no value.
 * @note    Call it inside the editor window, before anything that reads Esc: Esc only closes the list.
 */
std::optional<PlacingMode> PartPicker::Draw(const std::vector<const SavedSubcircuit *> &blocks) {
    OpenIfAsked();
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
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * PART_PICKER_WIDTH);
    if (ImGui::InputTextWithHint("##query", "Find a part", &m_Query)) {
        m_Highlighted = 0;
    }
    const PartMatches matches = FindMatches(m_Query, blocks);
    const bool moved = MoveHighlight(matches.GetCount());
    std::optional<PlacingMode> picked;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (matches.GetCount() > 0 && enter) {
        picked = matches.Get(m_Highlighted);
    }
    if (matches.GetCount() == 0) {
        ImGui::TextDisabled("No part matches");
    }
    if (std::optional<PlacingMode> clicked = DrawMatches(matches, moved)) {
        picked = std::move(clicked);
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

/**
 * @brief   How many parts and blocks match.
 */
std::size_t PartPicker::PartMatches::GetCount() const {
    return Parts.size() + Blocks.size();
}

/**
 * @brief   What placing the match at a position of the list means: a part, or a block after the parts.
 */
PlacingMode PartPicker::PartMatches::Get(const std::size_t index) const {
    if (index < Parts.size()) {
        return PlacingMode{.Type = Parts[index]};
    }
    return PlacingMode{.Type = Core::ComponentType::SUBCIRCUIT, .Block = Blocks[index - Parts.size()]->Name};
}

/**
 * @brief   The parts whose names hold the query, and the blocks whose names hold it as typed.
 */
PartPicker::PartMatches PartPicker::FindMatches(const std::string &query,
                                                const std::vector<const SavedSubcircuit *> &blocks) {
    PartMatches matches{.Parts = FindParts(query), .Blocks = {}};
    for (const SavedSubcircuit *definition : blocks) {
        if (definition->Name.contains(query)) {
            matches.Blocks.push_back(definition);
        }
    }
    return matches;
}

/**
 * @brief   Opens the picker with an empty query on Space, while the editor has the focus and no field is active, or
 *          when a menu asked for it.
 */
void PartPicker::OpenIfAsked() {
    const bool space_pressed =
        ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (std::exchange(m_IsOpenRequested, false) || space_pressed) {
        m_Query.clear();
        m_Highlighted = 0;
        ImGui::OpenPopup(PART_PICKER_POPUP);
    }
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
 *          blocks.
 * @details Returns the match clicked this frame, or none.
 */
std::optional<PlacingMode> PartPicker::DrawMatches(const PartMatches &matches, const bool moved) {
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
    std::optional<PlacingMode> clicked;
    for (std::size_t index = 0; index < matches.GetCount(); ++index) {
        const bool is_highlighted = index == m_Highlighted;
        bool is_clicked = false;
        if (index < matches.Parts.size()) {
            is_clicked = SelectablePartRow(matches.Parts[index], is_highlighted, width, m_Preferences.Symbols);
        } else {
            ImGui::PushID(static_cast<int>(index));
            const std::string label = std::format("Block: {}", matches.Blocks[index - matches.Parts.size()]->Name);
            is_clicked =
                ImGui::Selectable(label.c_str(), is_highlighted, ImGuiSelectableFlags_None, {width, row_height});
            ImGui::PopID();
        }
        if (is_clicked) {
            clicked = matches.Get(index);
        }
        if (moved && is_highlighted) {
            ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();
    return clicked;
}

} // namespace GUI
