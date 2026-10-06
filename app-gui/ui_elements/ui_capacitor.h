/**
 * @file    ui_capacitor.h
 * @brief   Schematic symbol of a capacitor.
 */

#ifndef IMCSIM_UI_CAPACITOR_H
#define IMCSIM_UI_CAPACITOR_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UICapacitor
 * @brief   Capacitor drawn as two parallel plates between its terminals.
 */
class UICapacitor : public UIElement {
public:
    UICapacitor(ImVec2 position, Rotation rotation);
    ~UICapacitor() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_CAPACITOR_H
