/**
 * @file    ui_logic_box.cpp
 * @brief   The box on a lead that logic inputs and logic outputs share, with the digit of their level in it.
 */

#include "ui_elements/ui_logic_box.h"

#include "drawing/theme.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

namespace GUI {

namespace {

/// The digit is the level of the part, so it is larger than the labels, as tall as most of the box.
constexpr float DIGIT_SCALE = 1.0f;
/// Above this relative luminance a fill reads as light, so the digit on it is dark.
constexpr float LIGHT_FILL_LUMINANCE = 0.5f;
constexpr ImU32 DARK_DIGIT = IM_COL32(20, 20, 20, 255);
constexpr ImU32 LIGHT_DIGIT = IM_COL32(255, 255, 255, 255);

/**
 * @brief   A digit color that reads on a fill: dark on a light one, white on a dark one, whatever the theme.
 */
ImU32 GetDigitColorOn(const ImVec4 fill) {
    // Rec. 709 weights, which follow how bright each primary looks
    const float luminance = 0.2126f * fill.x + 0.7152f * fill.y + 0.0722f * fill.z;
    return luminance > LIGHT_FILL_LUMINANCE ? DARK_DIGIT : LIGHT_DIGIT;
}

} // namespace

/**
 * @brief   Fills the box with the accent of the theme and writes the digit over it in a color that reads on it, as a
 *          lit indicator.
 * @param[in,out] canvas  Where it is drawn.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     digit   The level to show, such as "1".
 */
void UILogicBox::DrawLit(SchematicCanvas &canvas, const ViewTransform &view, const char *digit) const {
    const ImVec2 first = LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP);
    const ImVec2 second = LocalToScreen(view, BOX_HALF_WIDTH, BOX_BOTTOM);
    canvas.AddRectFilled({std::min(first.x, second.x), std::min(first.y, second.y)},
                         {std::max(first.x, second.x), std::max(first.y, second.y)},
                         ImGui::ColorConvertFloat4ToU32(GetPalette().Accent));
    DrawBox(canvas, view, ImGui::ColorConvertFloat4ToU32(GetPalette().AccentHot));
    DrawDigit(canvas, view, digit, GetDigitColorOn(GetPalette().Accent));
}

/**
 * @brief   Creates the box of a logic input or output placed on the grid.
 * @param[in] component  The simulation component of the part.
 * @param[in] position   Grid position of the terminal, in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UILogicBox::UILogicBox(std::unique_ptr<Core::Component> component, const GridPoint position, const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

/**
 * @brief   The lead from the terminal up to the box.
 */
void UILogicBox::DrawLead(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, BOX_BOTTOM), color, LINE_THICKNESS);
}

void UILogicBox::DrawBox(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::array<ImVec2, 4> box = {
        LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP), LocalToScreen(view, BOX_HALF_WIDTH, BOX_TOP),
        LocalToScreen(view, BOX_HALF_WIDTH, BOX_BOTTOM), LocalToScreen(view, -BOX_HALF_WIDTH, BOX_BOTTOM)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The digit in the middle of the box, sized with the zoom and left out when too small to read.
 */
void UILogicBox::DrawDigit(SchematicCanvas &canvas, const ViewTransform &view, const char *digit,
                           const ImU32 color) const {
    const float middle = (BOX_TOP + BOX_BOTTOM) / 2.0f;
    const ImVec2 center = LocalToScreen(view, 0.0f, middle);
    // One grid unit long in local space, so its length on screen is the current zoom
    const ImVec2 unit = LocalToScreen(view, 0.0f, middle + 1.0f) - center;
    const float font_size = std::hypot(unit.x, unit.y) * DIGIT_SCALE;
    if (font_size < MIN_LABEL_SIZE) {
        return;
    }
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, digit);
    canvas.AddText(font, font_size, center - text_size / 2.0f, color, digit);
}

std::vector<GridPoint> UILogicBox::GetLocalTerminals() const {
    return {{0, 0}};
}

/**
 * @brief   The terminal is at the origin and its lead goes up to the box.
 */
GridPoint UILogicBox::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UILogicBox::GetLocalBounds() const {
    return {{-BOX_HALF_WIDTH, BOX_TOP}, {BOX_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
