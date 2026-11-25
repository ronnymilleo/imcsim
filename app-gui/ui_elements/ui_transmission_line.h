/**
 * @file    ui_transmission_line.h
 * @brief   Schematic symbol of a two-port coaxial TEM transmission line.
 */

#ifndef IMCSIM_UI_TRANSMISSION_LINE_H
#define IMCSIM_UI_TRANSMISSION_LINE_H

#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UITransmissionLine
 * @brief   Coaxial cylinder symbol with port 1 on the left, + at (-2, -1) and - at (-2, 1), and port 2 on the right,
 *          + at (2, -1) and - at (2, 1).
 * @details The + leads feed the inner conductor ends at y = 0, while the - leads connect to the outer shield cylinder.
 *          The name is labeled above, and the characteristic impedance with the propagation delay below.
 */
class UITransmissionLine : public UIElement {
public:
    UITransmissionLine(GridPoint position, Rotation rotation);
    ~UITransmissionLine() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_TRANSMISSION_LINE_H
