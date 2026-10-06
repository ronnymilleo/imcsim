/**
 * @file    ui_inductor.h
 * @brief   Schematic symbol of an inductor.
 */

#ifndef IMCSIM_UI_INDUCTOR_H
#define IMCSIM_UI_INDUCTOR_H

#include "ui_element.h"

namespace GUI {

/**
 * @class   UIInductor
 * @brief   Inductor drawn as a row of semicircular coil turns between its terminals.
 */
class UIInductor : public UIElement {
public:
    UIInductor(ImVec2 position, Rotation rotation);
    ~UIInductor() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_INDUCTOR_H
