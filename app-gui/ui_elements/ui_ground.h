/**
 * @file    ui_ground.h
 * @brief   Schematic symbol of a ground reference.
 */

#ifndef IMCSIM_UI_GROUND_H
#define IMCSIM_UI_GROUND_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIGround
 * @brief   Ground drawn as three bars of decreasing width below its terminal.
 * @details The single terminal is the element position (local origin).
 */
class UIGround : public UIElement {
public:
    UIGround(GridPoint position, Rotation rotation);
    ~UIGround() override = default;

protected:
    void DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_GROUND_H
