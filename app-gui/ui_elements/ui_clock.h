/**
 * @file    ui_clock.h
 * @brief   Schematic symbol of a digital clock.
 */

#ifndef IMCSIM_UI_CLOCK_H
#define IMCSIM_UI_CLOCK_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIClock
 * @brief   A box holding a square wave, between the two leads of the clock.
 * @details The terminals are at (-2, 0) and (2, 0), like a voltage source. The frequency and the duty cycle are
 *          written under the part, so the symbol stays the same at every setting.
 */
class UIClock : public UIElement {
public:
    UIClock(GridPoint position, Rotation rotation);
    ~UIClock() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_CLOCK_H
