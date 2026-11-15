/**
 * @file    ui_logic_output.cpp
 * @brief   Schematic symbol of a logic output that shows the logic level of its node.
 */

#include "ui_elements/ui_logic_output.h"

#include "circuit/components/logic_output.h"
#include <memory>

namespace GUI {

namespace {

/// Half the width of the arrow on the lead, which points into the box.
constexpr float ARROW_HALF_WIDTH = 0.2f;

} // namespace

/**
 * @brief   Creates a logic output placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UILogicOutput::UILogicOutput(const GridPoint position, const Rotation rotation)
    : UILogicBox(std::make_unique<Core::LogicOutput>(), position, rotation) {
}

/**
 * @brief   Shows what the output reads: the digit in the box, lit when it reads 1 and the effect is on.
 * @param[in,out] canvas  Where it is drawn.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     state   The level of the node, as a gate reads it.
 * @param[in]     color   Color of the digit when the box is not lit.
 * @param[in]     is_lit  Whether to light the box, for an output that reads 1 with the Logic Lights effect on.
 */
void UILogicOutput::DrawReading(SchematicCanvas &canvas, const ViewTransform &view, const LogicState state,
                                const ImU32 color, const bool is_lit) const {
    const char *digit = GetLogicStateName(state);
    if (is_lit) {
        DrawLit(canvas, view, digit);
    } else {
        DrawDigit(canvas, view, digit, color);
    }
}

/**
 * @brief   The lead goes up from the terminal to the box, with an arrow that points into it.
 */
void UILogicOutput::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLead(canvas, view, color);
    canvas.AddTriangleFilled(LocalToScreen(view, -ARROW_HALF_WIDTH, BOX_BOTTOM + ARROW_HALF_WIDTH * 2.0f),
                             LocalToScreen(view, ARROW_HALF_WIDTH, BOX_BOTTOM + ARROW_HALF_WIDTH * 2.0f),
                             LocalToScreen(view, 0.0f, BOX_BOTTOM), color);
}

/**
 * @brief   The empty box; the reading goes in it through DrawReading().
 */
void UILogicOutput::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                               const SymbolStyle /*style*/) const {
    DrawBox(canvas, view, color);
}

} // namespace GUI
