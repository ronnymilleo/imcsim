/**
 * @file    ui_subcircuit.h
 * @brief   Rectangular symbol of a project-local subcircuit instance.
 */

#ifndef IMCSIM_UI_SUBCIRCUIT_H
#define IMCSIM_UI_SUBCIRCUIT_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UISubcircuit
 * @brief   A box with the name of the block below it, its inputs on the left and its outputs on the right.
 * @details The box grows with the side that has more pins. Each pin is labelled inside the box.
 */
class UISubcircuit : public UIElement {
public:
    UISubcircuit(GridPoint position, Rotation rotation);

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_SUBCIRCUIT_H
