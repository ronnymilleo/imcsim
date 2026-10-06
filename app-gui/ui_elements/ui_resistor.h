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
 * @brief   Resistor drawn as a rectangle between its terminals.
 */
class UIResistor : public UIElement {
public:
    UIResistor(ImVec2 position, Rotation rotation);
    ~UIResistor() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_RESISTOR_H
