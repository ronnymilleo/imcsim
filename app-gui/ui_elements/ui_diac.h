/**
 * @file    ui_diac.h
 * @brief   Schematic symbol of a DIAC.
 */

#ifndef IMCSIM_UI_DIAC_H
#define IMCSIM_UI_DIAC_H

#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UIDiac
 * @brief   Two anti-parallel triangles between two bars, MT2 to MT1 left to right, with no gate.
 * @details MT2 is terminal 1 at (-2, 0) and MT1 terminal 2 at (2, 0). The name is drawn above the symbol and the
 *          breakover voltage below it.
 */
class UIDiac : public UIElement {
public:
    UIDiac(GridPoint position, Rotation rotation);
    ~UIDiac() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_DIAC_H
