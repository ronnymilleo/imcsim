/**
 * @file    ui_thyristor.h
 * @brief   Schematic symbol of a thyristor (SCR), a TRIAC or a GTO.
 */

#ifndef IMCSIM_UI_THYRISTOR_H
#define IMCSIM_UI_THYRISTOR_H

#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UIThyristor
 * @brief   The diode symbol of a thyristor, anode (MT2) to cathode (MT1) left to right, with the gate lead leaving the
 *          cathode bar for its third terminal below.
 * @details One class serves the SCR, the TRIAC and the GTO: a GTO adds two arrowheads, one each way, on the gate
 *          lead; a TRIAC draws two anti-parallel triangles, one each way, sharing
 *          the MT1 bar. The anode (MT2) is terminal 1 at (-2, 0), the cathode (MT1) terminal 2 at (2, 0) and the gate
 *          terminal 3 at (0, 2). The name is drawn above the symbol and the holding current below it.
 */
class UIThyristor : public UIElement {
public:
    UIThyristor(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIThyristor() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_THYRISTOR_H