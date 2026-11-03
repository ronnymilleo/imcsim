/**
 * @file    ui_subcircuit_pin.h
 * @brief   Schematic symbols of explicit subcircuit input and output markers.
 */

#ifndef IMCSIM_UI_SUBCIRCUIT_PIN_H
#define IMCSIM_UI_SUBCIRCUIT_PIN_H

#include "ui_element.h"

namespace GUI {

class UISubcircuitPin : public UIElement {
public:
    UISubcircuitPin(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UISubcircuitPin() override = default;

protected:
    void DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    std::vector<GridPoint> GetLocalTerminals() const override;
    GridPoint GetLocalTerminalInward(std::size_t terminal) const override;
    LocalBounds GetLocalBounds() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_SUBCIRCUIT_PIN_H
