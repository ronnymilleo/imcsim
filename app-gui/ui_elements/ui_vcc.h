/**
 * @file    ui_vcc.h
 * @brief   Schematic symbol of a VCC supply rail.
 */

#ifndef IMCSIM_UI_VCC_H
#define IMCSIM_UI_VCC_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIVCC
 * @brief   Supply rail drawn as a bar above its terminal, labeled VCC.
 * @details The single terminal is the element position (local origin).
 */
class UIVCC : public UIElement {
public:
    UIVCC(ImVec2 position, Rotation rotation);
    ~UIVCC() override = default;

protected:
    void DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_VCC_H
