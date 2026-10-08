/**
 * @file    ui_mosfet.h
 * @brief   Schematic symbol of an N- or P-channel enhancement MOSFET.
 */

#ifndef IMCSIM_UI_MOSFET_H
#define IMCSIM_UI_MOSFET_H

#include "ui_transistor.h"

namespace GUI {

/**
 * @class   UIMOSFET
 * @brief   Enhancement MOSFET drawn as a gate plate beside a broken channel, with the body tied to the source.
 * @details The arrow on the body points into the channel for NMOS and out of it for PMOS.
 */
class UIMOSFET : public UITransistor {
public:
    UIMOSFET(Core::ComponentType type, GridPoint position, Rotation rotation);
    ~UIMOSFET() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    const char *GetModelName() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_MOSFET_H
