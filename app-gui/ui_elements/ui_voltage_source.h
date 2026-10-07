/**
 * @file    ui_voltage_source.h
 * @brief   Schematic symbol of an independent voltage source.
 */

#ifndef IMCSIM_UI_VOLTAGE_SOURCE_H
#define IMCSIM_UI_VOLTAGE_SOURCE_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIVoltageSource
 * @brief   Schematic symbol of an independent voltage source.
 */
class UIVoltageSource : public UIElement {
public:
    UIVoltageSource(GridPoint position, Rotation rotation);
    ~UIVoltageSource() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_VOLTAGE_SOURCE_H
