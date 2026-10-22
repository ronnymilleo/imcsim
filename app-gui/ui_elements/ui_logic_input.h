/**
 * @file    ui_logic_input.h
 * @brief   Schematic symbol of a logic input that drives a circuit high or low.
 */

#ifndef IMCSIM_UI_LOGIC_INPUT_H
#define IMCSIM_UI_LOGIC_INPUT_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UILogicInput
 * @brief   A box above its terminal that shows a 1 while high and a 0 while low, like a supply rail.
 * @details The single terminal is the element position (local origin); the box is a constant source from that
 *          terminal to ground.
 */
class UILogicInput : public UIElement {
public:
    UILogicInput(GridPoint position, Rotation rotation);
    ~UILogicInput() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_LOGIC_INPUT_H
