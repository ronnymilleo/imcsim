/**
 * @file    ui_logic_output.cpp
 * @brief   Schematic symbol of a logic output that shows the logic level of its node.
 */

#include "ui_elements/ui_logic_output.h"

#include "components/logic_output.h"
#include <array>
#include <cfloat>
#include <cmath>

namespace GUI {

namespace {

// The box of the logic input, so the two read as a pair
constexpr float LEAD_LENGTH = 0.6f;
constexpr float BOX_HALF_WIDTH = 0.8f;
constexpr float BOX_TOP = -2.0f;
constexpr float BOX_BOTTOM = -LEAD_LENGTH;
/// Half the width of the arrow on the lead, which points into the box.
constexpr float ARROW_HALF_WIDTH = 0.2f;
/// The digit is as tall as most of the box, like the one of the logic input.
constexpr float DIGIT_SCALE = 1.0f;
/// The fill of a lit output, a green that reads on light and dark canvases alike.
constexpr ImU32 LIT_FILL = IM_COL32(64, 200, 96, 255);
/// The digit over the lit fill.
constexpr ImU32 LIT_DIGIT = IM_COL32(16, 32, 20, 255);

} // namespace

/**
 * @brief   Creates a logic output placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UILogicOutput::UILogicOutput(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::LogicOutput>(), position, rotation) {
}

/**
 * @brief   Shows what the output reads: the digit in the box, and the box lit when it reads 1 and the effect is on.
 * @param[in,out] canvas  Where it is drawn.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     state   The level of the node, as a gate reads it.
 * @param[in]     color   Color of the digit when the box is not lit.
 * @param[in]     is_lit  Whether to fill the box, for an output that reads 1 with its live effect on.
 */
void UILogicOutput::DrawReading(SchematicCanvas &canvas, const ViewTransform &view, const LogicState state,
                                const ImU32 color, const bool is_lit) const {
    if (is_lit) {
        const ImVec2 first = LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP);
        const ImVec2 second = LocalToScreen(view, BOX_HALF_WIDTH, BOX_BOTTOM);
        canvas.AddRectFilled({std::min(first.x, second.x), std::min(first.y, second.y)},
                             {std::max(first.x, second.x), std::max(first.y, second.y)}, LIT_FILL);
    }
    const float middle = (BOX_TOP + BOX_BOTTOM) / 2.0f;
    const ImVec2 center = LocalToScreen(view, 0.0f, middle);
    // One grid unit long in local space, so its length on screen is the current zoom
    const ImVec2 unit = LocalToScreen(view, 0.0f, middle + 1.0f) - center;
    const float font_size = std::hypot(unit.x, unit.y) * DIGIT_SCALE;
    if (font_size < MIN_LABEL_SIZE) {
        return;
    }
    const char *digit = GetLogicStateName(state);
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, digit);
    canvas.AddText(font, font_size, center - text_size / 2.0f, is_lit ? LIT_DIGIT : color, digit);
}

/**
 * @brief   The lead goes up from the terminal to the box, with an arrow that points into it.
 */
void UILogicOutput::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, BOX_BOTTOM), color, LINE_THICKNESS);
    canvas.AddTriangleFilled(LocalToScreen(view, -ARROW_HALF_WIDTH, BOX_BOTTOM + ARROW_HALF_WIDTH * 2.0f),
                             LocalToScreen(view, ARROW_HALF_WIDTH, BOX_BOTTOM + ARROW_HALF_WIDTH * 2.0f),
                             LocalToScreen(view, 0.0f, BOX_BOTTOM), color);
}

/**
 * @brief   The empty box; the reading goes in it through DrawReading().
 */
void UILogicOutput::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                               const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {
        LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP), LocalToScreen(view, BOX_HALF_WIDTH, BOX_TOP),
        LocalToScreen(view, BOX_HALF_WIDTH, BOX_BOTTOM), LocalToScreen(view, -BOX_HALF_WIDTH, BOX_BOTTOM)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

std::vector<GridPoint> UILogicOutput::GetLocalTerminals() const {
    return {{0, 0}};
}

/**
 * @brief   The terminal is at the origin and its lead goes up to the box.
 */
GridPoint UILogicOutput::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UILogicOutput::GetLocalBounds() const {
    return {{-BOX_HALF_WIDTH, BOX_TOP}, {BOX_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
