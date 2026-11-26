/**
 * @file    ui_igbt.h
 * @brief   Schematic symbol of an N-channel IGBT.
 */

#ifndef IMCSIM_UI_IGBT_H
#define IMCSIM_UI_IGBT_H

#include "ui_elements/ui_transistor.h"

namespace GUI {

/**
 * @class   UIIGBT
 * @brief   IGBT drawn as the insulated gate plate of a MOSFET beside the body of a bipolar transistor, with the arrow
 *          on the emitter pointing out.
 */
class UIIGBT : public UITransistor {
public:
    UIIGBT(GridPoint position, Rotation rotation);
    ~UIIGBT() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    const char *GetModelName() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_IGBT_H
