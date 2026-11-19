/**
 * @file    ui_vcc.h
 * @brief   Schematic symbol of a VCC supply rail.
 */

#ifndef IMCSIM_UI_VCC_H
#define IMCSIM_UI_VCC_H

#include "ui_elements/ui_single_terminal_element.h"

namespace GUI {

/**
 * @class   UIVCC
 * @brief   Supply rail drawn as a bar above its terminal, labeled VCC.
 * @details The single terminal is the element position (local origin).
 */
class UIVCC : public UISingleTerminalElement {
public:
    UIVCC(GridPoint position, Rotation rotation);
    ~UIVCC() override = default;

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

    LocalBounds GetLocalBounds() const override;
    ImVec2 GetLeadEnd() const override;
};

} // namespace GUI

#endif // IMCSIM_UI_VCC_H
