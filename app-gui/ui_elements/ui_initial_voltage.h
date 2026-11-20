/**
 * @file    ui_initial_voltage.h
 * @brief   Schematic symbol of an initial voltage marker.
 */

#ifndef IMCSIM_UI_INITIAL_VOLTAGE_H
#define IMCSIM_UI_INITIAL_VOLTAGE_H

#include "ui_elements/ui_single_terminal_element.h"

namespace GUI {

/**
 * @class   UIInitialVoltage
 * @brief   A short lead ending in a small square, with "IC" and the voltage past it.
 * @details The terminal is at the foot of the lead, so a wire reaches it like it reaches a supply or a net label. The
 *          square is the only closed shape on a lead apart from the logic box, which is larger and holds a digit.
 */
class UIInitialVoltage : public UISingleTerminalElement {
public:
    UIInitialVoltage(GridPoint position, Rotation rotation);
    ~UIInitialVoltage() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;
    ImVec2 GetLeadEnd() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_INITIAL_VOLTAGE_H
