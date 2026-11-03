/**
 * @file    ui_subcircuit_pin.h
 * @brief   Schematic symbol of the pin markers that give a block its connections.
 */

#ifndef IMCSIM_UI_SUBCIRCUIT_PIN_H
#define IMCSIM_UI_SUBCIRCUIT_PIN_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UISubcircuitPin
 * @brief   A square with an arrow inside, on a short lead, with the pin name beyond the square.
 * @details The square stands for the edge of the block and the arrow points from it into the circuit, toward the lead
 *          and the terminal. The pin goes on the edge the arrow comes from: an arrow pointing right puts it on the left
 *          of the block's box, one pointing down on its top.
 */
class UISubcircuitPin : public UIElement {
public:
    UISubcircuitPin(GridPoint position, Rotation rotation);
    ~UISubcircuitPin() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_SUBCIRCUIT_PIN_H
