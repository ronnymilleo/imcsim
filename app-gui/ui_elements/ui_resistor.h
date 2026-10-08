/**
 * @file    ui_resistor.h
 * @brief   Schematic symbol of a resistor.
 */

#ifndef IMCSIM_UI_RESISTOR_H
#define IMCSIM_UI_RESISTOR_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIResistor
 * @brief   Resistor drawn as a rectangle (IEC) or a zig-zag (ANSI) between its terminals.
 */
class UIResistor : public UIElement {
public:
    UIResistor(GridPoint position, Rotation rotation);
    ~UIResistor() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_RESISTOR_H
