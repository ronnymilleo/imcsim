/**
 * @file    ui_transformer.h
 * @brief   Schematic symbol of a transformer.
 */

#ifndef IMCSIM_UI_TRANSFORMER_H
#define IMCSIM_UI_TRANSFORMER_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UITransformer
 * @brief   Two coils facing each other across two lines for the core, with a dot at the + end of each.
 * @details The primary is on the left, + at (-2, -1) and - at (-2, 1), and the secondary on the right, + at (2, -1)
 *          and - at (2, 1). The name is labeled above, and the primary inductance with the turns ratio below.
 */
class UITransformer : public UIElement {
public:
    UITransformer(GridPoint position, Rotation rotation);
    ~UITransformer() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;

private:
    void DrawCoil(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, float x, float bulge) const;
};

} // namespace GUI

#endif // IMCSIM_UI_TRANSFORMER_H
