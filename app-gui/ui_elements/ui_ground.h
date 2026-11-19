/**
 * @file    ui_ground.h
 * @brief   Schematic symbol of a ground reference.
 */

#ifndef IMCSIM_UI_GROUND_H
#define IMCSIM_UI_GROUND_H

#include "ui_elements/ui_single_terminal_element.h"

namespace GUI {

/**
 * @class   UIGround
 * @brief   Ground drawn as three bars of decreasing width below its terminal.
 * @details The single terminal is the element position (local origin).
 */
class UIGround : public UISingleTerminalElement {
public:
    UIGround(GridPoint position, Rotation rotation);
    ~UIGround() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;
    ImVec2 GetLeadEnd() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_GROUND_H
