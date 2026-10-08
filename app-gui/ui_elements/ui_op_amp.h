/**
 * @file    ui_op_amp.h
 * @brief   Schematic symbol of an op-amp with its supply pins.
 */

#ifndef IMCSIM_UI_OP_AMP_H
#define IMCSIM_UI_OP_AMP_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIOpAmp
 * @brief   A triangle pointing at its output, with the inputs on the left and the supply pins above and below.
 * @details Terminals, in component order: output at (3, 0), + input at (-3, 1), - input at (-3, -1), V+ at (0, -2)
 *          and V- at (0, 2). The inverting input is on top, as in most textbook amplifiers, so feedback runs over
 *          the triangle; mirroring top to bottom swaps the inputs.
 */
class UIOpAmp : public UIElement {
public:
    UIOpAmp(GridPoint position, Rotation rotation);
    ~UIOpAmp() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_OP_AMP_H
