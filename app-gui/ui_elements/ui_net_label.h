/**
 * @file    ui_net_label.h
 * @brief   Schematic symbol of a net label.
 */

#ifndef IMCSIM_UI_NET_LABEL_H
#define IMCSIM_UI_NET_LABEL_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UINetLabel
 * @brief   A short lead ending in a small circle, with the name of the net past it.
 * @details The terminal is at the foot of the lead, so a wire reaches it like it reaches a supply or a ground.
 */
class UINetLabel : public UIElement {
public:
    UINetLabel(GridPoint position, Rotation rotation);
    ~UINetLabel() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_NET_LABEL_H
