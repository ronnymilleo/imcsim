/**
 * @file    ui_voltage_source.h
 * @brief   Schematic symbol of an independent voltage source.
 */

#ifndef IMCSIM_UI_VOLTAGE_SOURCE_H
#define IMCSIM_UI_VOLTAGE_SOURCE_H

#include "ui_source.h"

namespace GUI {

/**
 * @class   UIVoltageSource
 * @brief   Schematic symbol of an independent voltage source, marked with its polarity.
 */
class UIVoltageSource : public UISource {
public:
    UIVoltageSource(GridPoint position, Rotation rotation);
    ~UIVoltageSource() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_VOLTAGE_SOURCE_H
