/**
 * @file    ui_vcc.h
 * @brief   Schematic symbol of a VCC supply rail.
 */

#ifndef IMCSIM_UI_VCC_H
#define IMCSIM_UI_VCC_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIVCC
 * @brief   Supply rail drawn as a bar above its terminal, labeled VCC.
 * @details The single terminal is the element position (local origin).
 */
class UIVCC : public UIElement {
public:
    UIVCC(GridPoint position, Rotation rotation);
    ~UIVCC() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_VCC_H
