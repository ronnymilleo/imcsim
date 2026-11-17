/**
 * @file    ui_nodeset.h
 * @brief   Schematic symbol of a nodeset marker.
 */

#ifndef IMCSIM_UI_NODESET_H
#define IMCSIM_UI_NODESET_H

#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UINodeset
 * @brief   A short lead ending in a small diamond, with "NS" and the voltage past it.
 * @details The terminal is at the foot of the lead, so a wire reaches it like it reaches a supply or a net label. The
 *          diamond tells it from the square of an initial voltage, which holds its node where this only guesses.
 */
class UINodeset : public UIElement {
public:
    UINodeset(GridPoint position, Rotation rotation);
    ~UINodeset() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_NODESET_H
