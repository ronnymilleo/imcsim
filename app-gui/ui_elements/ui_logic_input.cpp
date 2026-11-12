/**
 * @file    ui_logic_input.cpp
 * @brief   Schematic symbol of a logic input that drives a circuit high or low.
 */

#include "ui_elements/ui_logic_input.h"

#include "circuit/components/logic_input.h"
#include <memory>

namespace GUI {

namespace {

constexpr float LABEL_OFFSET = 0.3f;

} // namespace

/**
 * @brief   Creates a logic input placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UILogicInput::UILogicInput(const GridPoint position, const Rotation rotation)
    : UILogicBox(std::make_unique<Core::LogicInput>(), position, rotation) {
}

void UILogicInput::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLead(canvas, view, color);
}

/**
 * @brief   The box shows the level the part drives, 1 when high and 0 when low.
 */
void UILogicInput::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                              const SymbolStyle /*style*/) const {
    DrawBox(canvas, view, color);
    DrawDigit(canvas, view, static_cast<const Core::LogicInput &>(GetComponent()).IsHigh() ? "1" : "0", color);
}

/**
 * @brief   The name sits above the box.
 */
void UILogicInput::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(canvas, view, {0.0f, BOX_TOP - LABEL_OFFSET}, {0.0f, -1.0f}, GetComponent().GetName(), color);
}

} // namespace GUI
