/**
 * @file    part_rail.cpp
 * @brief   The column of part buttons beside the schematic, from the part catalog.
 */

#include "windows/part_rail.h"

#include "base/theme.h"
#include "canvas/schematic_canvas.h"
#include "schematic/element_factory.h"
#include "schematic/ready_blocks.h"
#include "windows/tool_icons.h"
#include <algorithm>
#include <format>
#include <span>
#include <string>

namespace GUI {

namespace {

/// The corner mark of a group button, which tells it opens a list, as a share of the button.
constexpr float GROUP_MARK_SHARE = 0.22f;
/// Space between the buttons of a section, in font sizes.
constexpr float BUTTON_SPACING = 0.2f;
/// Space around the separator between sections, in font sizes.
constexpr float SECTION_GAP = 0.35f;
/// Holding a group button this long, in seconds, opens its list, as a right click does.
constexpr float HOLD_TO_LIST_SECONDS = 0.35f;

/**
 * @brief   Square rail button; the active one uses the primary colors, as the part being placed.
 */
bool RailButton(const char *id, const bool is_active) {
    const float side = GetToolButtonSide();
    return is_active ? PrimaryButton(id, {side, side}) : ImGui::Button(id, {side, side});
}

/**
 * @brief   A small triangle in the bottom right corner of the last button, which tells it opens a list.
 */
void DrawGroupMark() {
    const ImVec2 corner = ImGui::GetItemRectMax();
    const float size = GetToolButtonSide() * GROUP_MARK_SHARE;
    const float inset = size * 0.4f;
    ImGui::GetWindowDrawList()->AddTriangleFilled({corner.x - inset, corner.y - inset - size},
                                                  {corner.x - inset, corner.y - inset},
                                                  {corner.x - inset - size, corner.y - inset}, GetIconColor(true));
}

/**
 * @brief   A thin line across the rail between two sections.
 */
void DrawSectionSeparator() {
    const float gap = ImGui::GetFontSize() * SECTION_GAP;
    ImGui::Dummy({0.0f, gap});
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({start.x, start.y}, {start.x + GetToolButtonSide(), start.y},
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy({0.0f, gap});
}

/**
 * @brief   Whether the last button asks for its list: a right click, or the left button held on it.
 */
bool IsListAsked() {
    return ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
           (ImGui::IsItemActive() && ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left] >= HOLD_TO_LIST_SECONDS);
}

/**
 * @brief   Opens a list beside the rail, level with the button that asked for it.
 */
void OpenListBeside(const char *id) {
    ImGui::SetNextWindowPos({ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().y});
    ImGui::OpenPopup(id);
}

} // namespace

/**
 * @brief   Creates the rail, each group placing its first part until another is picked.
 * @param[in] preferences  The symbol standard of the part icons; they must outlive the rail.
 */
PartRail::PartRail(const Preferences &preferences) : m_Preferences(preferences) {
    for (const auto section : GetRailSections()) {
        for (const RailEntry &entry : section) {
            m_GroupChoices.push_back(entry.Parts.empty() ? Core::ComponentType::SUBCIRCUIT : entry.Parts.front());
        }
    }
    for (const ReadyBlock &block : GetReadyBlocks()) {
        auto group = MakeReadyBlock(block);
        m_ReadyBlockIcons.push_back(group && !group->Elements.empty() ? std::move(group->Elements.front()) : nullptr);
    }
}

/**
 * @brief   The width the rail takes beside the canvas.
 * @return  The width in pixels, a button and the padding around it.
 */
float PartRail::GetWidth() const {
    return GetToolButtonSide() + ImGui::GetStyle().WindowPadding.x * 2.0f;
}

/**
 * @brief   Draws the rail at the cursor, as tall as the canvas beside it, scrolling with the wheel when it is taller.
 * @param[in] mode    The mode of the editor, whose part shows as the one being placed.
 * @param[in] height  Height of the canvas, in pixels.
 * @return  The part or block clicked this frame, or All parts.
 */
RailRequest PartRail::Draw(const EditorMode &mode, const float height) {
    RailRequest request;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, ImGui::GetFontSize() * BUTTON_SPACING});
    if (ImGui::BeginChild("part_rail", {GetWidth(), height}, ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        std::size_t index = 0;
        const auto sections = GetRailSections();
        for (std::size_t section = 0; section < sections.size(); ++section) {
            if (section > 0) {
                DrawSectionSeparator();
            }
            for (const RailEntry &entry : sections[section]) {
                if (std::optional<EditorMode> placing = DrawEntry(entry, index, mode)) {
                    request.Mode = std::move(placing);
                }
                ++index;
            }
        }
        DrawSectionSeparator();
        if (RailButton("##all_parts", false)) {
            request.IsSearchAsked = true;
        }
        const ImVec2 center = GetItemCenter();
        const float dot = GetIconSize() * 0.08f;
        for (const float offset : {-3.0f, 0.0f, 3.0f}) {
            ImGui::GetWindowDrawList()->AddCircleFilled({center.x + offset * dot, center.y}, dot, GetIconColor(true));
        }
        ImGui::SetItemTooltip("All parts (Space)");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return request;
}

/**
 * @brief   One button of the rail.
 * @details Returns the mode that places what was clicked, or none.
 */
std::optional<EditorMode> PartRail::DrawEntry(const RailEntry &entry, const std::size_t index, const EditorMode &mode) {
    if (entry.IsReadyBlocks) {
        return DrawReadyBlocks();
    }
    if (!entry.IsGroup()) {
        const Core::ComponentType type = entry.Parts.front();
        if (DrawPartButton(type, mode, GetPartName(type))) {
            return PlacingMode{.Type = type};
        }
        return std::nullopt;
    }
    if (const std::optional<Core::ComponentType> type = DrawGroup(entry, index, mode)) {
        return PlacingMode{.Type = *type};
    }
    return std::nullopt;
}

/**
 * @brief   The button of one part, shown as the tool in use while that part is being placed.
 * @details Returns whether it was clicked.
 */
bool PartRail::DrawPartButton(const Core::ComponentType type, const EditorMode &mode, const char *tooltip) {
    ImGui::PushID(static_cast<int>(type));
    const bool is_clicked = RailButton("##part", GetPlacingType(mode) == type);
    ImGui::PopID();
    DrawPartIcon(ImGui::GetWindowDrawList(), type, GetItemCenter(), GetIconSize(), m_Preferences.Symbols);
    ImGui::SetItemTooltip("%s", tooltip);
    return is_clicked;
}

/**
 * @brief   The button places the part last picked in the group; holding it or a right click lists the others beside
 *          the rail, as its corner mark tells.
 * @details Returns the part to place, or none.
 */
std::optional<Core::ComponentType> PartRail::DrawGroup(const RailEntry &entry, const std::size_t index,
                                                       const EditorMode &mode) {
    Core::ComponentType &choice = m_GroupChoices[index];
    std::optional<Core::ComponentType> picked;
    ImGui::PushID(static_cast<int>(index));
    const std::string tooltip =
        std::format("{}: {} (hold or right click for the others)", entry.Name, GetPartName(choice));
    const bool is_clicked = DrawPartButton(choice, mode, tooltip.c_str());
    DrawGroupMark();
    if (IsListAsked()) {
        OpenListBeside("parts");
    } else if (is_clicked && !ImGui::IsPopupOpen("parts")) {
        picked = choice;
    }
    if (ImGui::BeginPopup("parts")) {
        ImGui::TextDisabled("%.*s", static_cast<int>(entry.Name.size()), entry.Name.data());
        const float side = GetToolButtonSide();
        float width = 0.0f;
        for (const Core::ComponentType type : entry.Parts) {
            width = std::max(width, ImGui::CalcTextSize(GetPartName(type)).x);
        }
        width += side + ImGui::GetStyle().ItemSpacing.x;
        for (const Core::ComponentType type : entry.Parts) {
            if (SelectablePartRow(type, GetPlacingType(mode) == type, width, m_Preferences.Symbols)) {
                choice = type;
                picked = type;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return picked;
}

/**
 * @brief   The button places the ready logic block last picked; holding it or a right click lists them all beside the
 *          rail, each telling what it does when hovered.
 * @details Returns the paste of the block to place, which brings its definition along, or none.
 */
std::optional<EditorMode> PartRail::DrawReadyBlocks() {
    const std::span<const ReadyBlock> blocks = GetReadyBlocks();
    if (blocks.empty()) {
        return std::nullopt;
    }
    std::optional<std::size_t> picked;
    ImGui::PushID("ready blocks");
    const float side = GetToolButtonSide();
    const bool is_clicked = RailButton("##block", false);
    DrawReadyBlockIcon(m_ReadyBlockChoice, GetItemCenter());
    DrawGroupMark();
    const ReadyBlock &chosen = blocks[m_ReadyBlockChoice];
    ImGui::SetItemTooltip("Logic block: %.*s (hold or right click for the others)",
                          static_cast<int>(chosen.Title.size()), chosen.Title.data());
    if (IsListAsked()) {
        OpenListBeside("blocks");
    } else if (is_clicked && !ImGui::IsPopupOpen("blocks")) {
        picked = m_ReadyBlockChoice;
    }
    if (ImGui::BeginPopup("blocks")) {
        ImGui::TextDisabled("Logic blocks");
        float width = 0.0f;
        for (const ReadyBlock &block : blocks) {
            width = std::max(width, ImGui::CalcTextSize(block.Title.data(), block.Title.data() + block.Title.size()).x);
        }
        width += side + ImGui::GetStyle().ItemSpacing.x;
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable("##block", index == m_ReadyBlockChoice, ImGuiSelectableFlags_None, {width, side})) {
                picked = index;
            }
            ImGui::PopID();
            ImGui::SetItemTooltip("%.*s", static_cast<int>(blocks[index].Description.size()),
                                  blocks[index].Description.data());
            DrawReadyBlockIcon(index, start + ImVec2(side, side) / 2.0f);
            ImGui::GetWindowDrawList()->AddText(
                start + ImVec2(side + ImGui::GetStyle().ItemSpacing.x, (side - ImGui::GetTextLineHeight()) / 2.0f),
                GetIconColor(true), blocks[index].Title.data(),
                blocks[index].Title.data() + blocks[index].Title.size());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (!picked) {
        return std::nullopt;
    }
    m_ReadyBlockChoice = *picked;
    auto group = MakeReadyBlock(blocks[*picked]);
    if (!group) {
        return std::nullopt;
    }
    return PastingMode{.Group = std::move(*group)};
}

/**
 * @brief   Draws a ready block as its icon: its box with the leads of its pins, scaled to a button.
 */
void PartRail::DrawReadyBlockIcon(const std::size_t index, const ImVec2 center) const {
    if (index >= m_ReadyBlockIcons.size() || !m_ReadyBlockIcons[index]) {
        return;
    }
    DrawListCanvas canvas(ImGui::GetWindowDrawList());
    m_ReadyBlockIcons[index]->DrawIcon(canvas, center, GetIconSize(), GetIconColor(true), m_Preferences.Symbols);
}

} // namespace GUI
