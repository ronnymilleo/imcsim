/**
 * @file    ui_potentiometer.h
 * @brief   Schematic symbol of a potentiometer.
 */

#ifndef IMCSIM_UI_POTENTIOMETER_H
#define IMCSIM_UI_POTENTIOMETER_H

#include "ui_resistor.h"

namespace GUI {

/**
 * @class   UIPotentiometer
 * @brief   A resistor with an arrow, the wiper, pointing at its middle from a third terminal above it.
 * @details Terminals 1 and 2 are the ends, at (-2, 0) and (2, 0), and terminal 3 is the wiper at (0, -2). The name,
 *          the value and the wiper position are labeled below in one line.
 */
class UIPotentiometer : public UIResistor {
public:
    UIPotentiometer(GridPoint position, Rotation rotation);
    ~UIPotentiometer() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_POTENTIOMETER_H
