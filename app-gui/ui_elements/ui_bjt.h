/**
 * @file    ui_bjt.h
 * @brief   Schematic symbol of an NPN or PNP bipolar transistor.
 */

#ifndef IMCSIM_UI_BJT_H
#define IMCSIM_UI_BJT_H

#include "ui_transistor.h"

namespace GUI {

/**
 * @class   UIBJT
 * @brief   Bipolar transistor drawn as a base bar with the collector and emitter leaving it at an angle.
 * @details The arrow on the emitter points out of the transistor for NPN and into it for PNP.
 */
class UIBJT : public UITransistor {
public:
    UIBJT(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIBJT() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    const char *GetModelName() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_BJT_H
