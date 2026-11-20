/**
 * @file    ui_clock.h
 * @brief   Schematic symbol of a digital clock.
 */

#ifndef IMCSIM_UI_CLOCK_H
#define IMCSIM_UI_CLOCK_H

#include "ui_elements/ui_single_terminal_element.h"

namespace GUI {

/**
 * @class   UIClock
 * @brief   A box holding a square wave above its terminal, like a supply rail.
 * @details The single terminal is the element position (local origin); the clock is a source from that terminal to
 *          ground. The frequency and the duty cycle are written beside the box, so the symbol stays the same at every
 *          setting.
 */
class UIClock : public UISingleTerminalElement {
public:
    UIClock(GridPoint position, Rotation rotation);
    ~UIClock() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;
    ImVec2 GetLeadEnd() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_CLOCK_H
