/**
 * @file    tool_icons.cpp
 * @brief   Icons of the editor toolbar and of the part lists, drawn as lines on the window.
 */

#include "drawing/tool_icons.h"

#include "drawing/schematic_canvas.h"
#include "editing/element_factory.h"
#include "helpers.h"
#include <array>
#include <cmath>
#include <numbers>

namespace GUI {

namespace {

/// Toolbar buttons are square, this many frame heights wide.
constexpr float TOOL_BUTTON_SCALE = 1.4f;
/// Share of a toolbar button its icon fills.
constexpr float ICON_SCALE = 0.7f;

/**
 * @struct  IconPlacement
 * @brief   Where and how large a toolbar icon is drawn, and the points of its unit square in screen space.
 */
struct IconPlacement {
    ImDrawList *DrawList;
    ImVec2 Center;
    float Size;
    ImU32 Color;
    /// -1 mirrors the icon horizontally, for Redo.
    float Flip;

    ImVec2 Point(const float x, const float y) const { return Center + ImVec2(x * Flip, y) * Size; }
};

void DrawSelectIcon(const IconPlacement &icon) {
    const std::array<ImVec2, 7> arrow = {icon.Point(-0.3f, -0.45f), icon.Point(-0.3f, 0.3f),  icon.Point(-0.12f, 0.13f),
                                         icon.Point(0.02f, 0.43f),  icon.Point(0.14f, 0.37f), icon.Point(0.0f, 0.08f),
                                         icon.Point(0.22f, 0.08f)};
    icon.DrawList->AddPolyline(arrow.data(), static_cast<int>(arrow.size()), icon.Color, ImDrawFlags_Closed,
                               LINE_THICKNESS);
}

void DrawWireIcon(const IconPlacement &icon) {
    const std::array<ImVec2, 4> wire = {icon.Point(-0.4f, 0.3f), icon.Point(0.0f, 0.3f), icon.Point(0.0f, -0.3f),
                                        icon.Point(0.4f, -0.3f)};
    icon.DrawList->AddPolyline(wire.data(), static_cast<int>(wire.size()), icon.Color, ImDrawFlags_None,
                               LINE_THICKNESS);
    icon.DrawList->AddCircleFilled(wire.front(), icon.Size * 0.08f, icon.Color);
    icon.DrawList->AddCircleFilled(wire.back(), icon.Size * 0.08f, icon.Color);
}

void DrawProbeIcon(const IconPlacement &icon) {
    icon.DrawList->AddLine(icon.Point(-0.45f, 0.45f), icon.Point(-0.12f, 0.12f), icon.Color, LINE_THICKNESS);
    icon.DrawList->AddLine(icon.Point(-0.12f, 0.12f), icon.Point(0.3f, -0.3f), icon.Color, icon.Size * 0.22f);
    icon.DrawList->AddLine(icon.Point(0.3f, -0.3f), icon.Point(0.45f, -0.45f), icon.Color, LINE_THICKNESS);
}

/**
 * @brief   Four corners of a frame.
 */
void DrawFitIcon(const IconPlacement &icon) {
    constexpr float CORNER = 0.4f;
    constexpr float ARM = 0.18f;
    for (const float x : {-CORNER, CORNER}) {
        for (const float y : {-CORNER, CORNER}) {
            const float inward_x = x < 0.0f ? ARM : -ARM;
            const float inward_y = y < 0.0f ? ARM : -ARM;
            const std::array<ImVec2, 3> corner = {icon.Point(x + inward_x, y), icon.Point(x, y),
                                                  icon.Point(x, y + inward_y)};
            icon.DrawList->AddPolyline(corner.data(), static_cast<int>(corner.size()), icon.Color, ImDrawFlags_None,
                                       LINE_THICKNESS);
        }
    }
}

/**
 * @brief   An arc over the top that turns back, with the arrowhead at its left end, mirrored for Redo.
 */
void DrawUndoIcon(const IconPlacement &icon) {
    const float radius = 0.28f;
    const ImVec2 middle = {0.05f, 0.05f};
    for (int step = 0; step <= 12; ++step) {
        const float angle = std::numbers::pi_v<float> * (1.0f + static_cast<float>(step) / 12.0f);
        icon.DrawList->PathLineTo(icon.Point(middle.x + radius * std::cos(angle), middle.y + radius * std::sin(angle)));
    }
    icon.DrawList->PathLineTo(icon.Point(middle.x + radius, middle.y + 0.25f));
    icon.DrawList->PathStroke(icon.Color, ImDrawFlags_None, LINE_THICKNESS);
    icon.DrawList->AddTriangleFilled(icon.Point(middle.x - radius, middle.y + 0.22f),
                                     icon.Point(middle.x - radius - 0.14f, middle.y - 0.02f),
                                     icon.Point(middle.x - radius + 0.14f, middle.y - 0.02f), icon.Color);
}

} // namespace

/**
 * @brief   The side of a square toolbar button, which follows the size of the font.
 * @return  The side in pixels.
 */
float GetToolButtonSide() {
    return ImGui::GetFrameHeight() * TOOL_BUTTON_SCALE;
}

/**
 * @brief   How large an icon is drawn in a toolbar button.
 * @return  The side of its unit square in pixels.
 */
float GetIconSize() {
    return GetToolButtonSide() * ICON_SCALE;
}

/**
 * @brief   The color an icon takes, which dims with its button.
 * @param[in] is_enabled  Whether the button can be pressed.
 * @return  The text color of the theme, or its disabled text color.
 */
ImU32 GetIconColor(const bool is_enabled) {
    return ImGui::GetColorU32(is_enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
}

/**
 * @brief   The middle of the last item drawn, where its icon goes.
 * @return  The point in screen pixels.
 */
ImVec2 GetItemCenter() {
    return (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) / 2.0f;
}

/**
 * @brief   Draws the icon of a tool, a line drawing on a unit square centered on the icon, y pointing down.
 * @param[in] draw_list  Where to draw.
 * @param[in] icon       Which tool.
 * @param[in] center     Middle of the icon, in screen pixels.
 * @param[in] size       Side of its unit square, in pixels.
 * @param[in] color      Color of its lines.
 */
void DrawToolIcon(ImDrawList *draw_list, const ToolIcon icon, const ImVec2 center, const float size,
                  const ImU32 color) {
    const IconPlacement placement{.DrawList = draw_list,
                                  .Center = center,
                                  .Size = size,
                                  .Color = color,
                                  .Flip = icon == ToolIcon::REDO ? -1.0f : 1.0f};
    switch (icon) {
    case ToolIcon::RUN:
        draw_list->AddTriangleFilled(placement.Point(-0.25f, -0.35f), placement.Point(0.35f, 0.0f),
                                     placement.Point(-0.25f, 0.35f), color);
        break;
    case ToolIcon::STOP:
        draw_list->AddRectFilled(placement.Point(-0.28f, -0.28f), placement.Point(0.28f, 0.28f), color);
        break;
    case ToolIcon::SELECT:
        DrawSelectIcon(placement);
        break;
    case ToolIcon::WIRE:
        DrawWireIcon(placement);
        break;
    case ToolIcon::PROBE:
        DrawProbeIcon(placement);
        break;
    case ToolIcon::FIT:
        DrawFitIcon(placement);
        break;
    case ToolIcon::UNDO:
    case ToolIcon::REDO:
        DrawUndoIcon(placement);
        break;
    default:
        break;
    }
}

/**
 * @brief   Draws the symbol of a part as its icon, in the text color.
 * @param[in] draw_list  Where to draw.
 * @param[in] type       Which part.
 * @param[in] center     Middle of the icon, in screen pixels.
 * @param[in] size       Side of the square the symbol fits in, in pixels.
 * @param[in] style      The symbol standard the schematic uses.
 */
void DrawPartIcon(ImDrawList *draw_list, const Core::ComponentType type, const ImVec2 center, const float size,
                  const SymbolStyle style) {
    DrawListCanvas canvas(draw_list);
    CreateElement(type, {0, 0}, Rotation::R0)->DrawIcon(canvas, center, size, GetIconColor(true), style);
}

/**
 * @brief   One row of a list of parts, as high as a toolbar button: the icon of the part, then its name.
 * @param[in] type         Which part.
 * @param[in] is_selected  Whether the row is highlighted.
 * @param[in] width        Width of the row, in pixels.
 * @param[in] style        The symbol standard the schematic uses.
 * @return  True on the frame the row is clicked.
 * @note    Push an ID first when the list holds the same part twice.
 */
bool SelectablePartRow(const Core::ComponentType type, const bool is_selected, const float width,
                       const SymbolStyle style) {
    const float row_height = GetToolButtonSide();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::PushID(static_cast<int>(type));
    const bool clicked = ImGui::Selectable("##part", is_selected, ImGuiSelectableFlags_None, {width, row_height});
    ImGui::PopID();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    DrawPartIcon(draw_list, type, start + ImVec2(row_height, row_height) / 2.0f, row_height * ICON_SCALE, style);
    const float text_y = (row_height - ImGui::GetTextLineHeight()) / 2.0f;
    draw_list->AddText(start + ImVec2(row_height + ImGui::GetStyle().ItemSpacing.x, text_y), GetIconColor(true),
                       GetPartName(type));
    return clicked;
}

} // namespace GUI
