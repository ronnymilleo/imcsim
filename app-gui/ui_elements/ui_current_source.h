/**
 * @file    ui_current_source.h
 * @brief   Schematic symbol of an independent current source.
 */

#ifndef IMCSIM_UI_CURRENT_SOURCE_H
#define IMCSIM_UI_CURRENT_SOURCE_H

#include "ui_source.h"

namespace GUI {

/**
 * @class   UICurrentSource
 * @brief   Schematic symbol of an independent current source, marked with an arrow in the current direction.
 */
class UICurrentSource : public UISource {
public:
    UICurrentSource(GridPoint position, Rotation rotation);
    ~UICurrentSource() override = default;

protected:
    void DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
};

} // namespace GUI

#endif // IMCSIM_UI_CURRENT_SOURCE_H
