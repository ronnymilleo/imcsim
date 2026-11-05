/**
 * @file    ui_fuse.h
 * @brief   Schematic symbol of a fuse.
 */

#ifndef IMCSIM_UI_FUSE_H
#define IMCSIM_UI_FUSE_H

#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @class   UIFuse
 * @brief   A rectangle with a line through it from lead to lead, which breaks with a gap when the fuse has blown.
 * @details The terminals are at (-2, 0) and (2, 0), like a resistor. Whether the fuse has blown is not part of the
 *          document: the overlays set it each frame from the result on display, with the strength of the flash that
 *          marks the instant it blew.
 */
class UIFuse : public UIElement {
public:
    UIFuse(GridPoint position, Rotation rotation);
    ~UIFuse() override = default;

    void SetDisplayState(bool is_blown, float flash_level);

protected:
    void DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color, SymbolStyle style) const override;
    void DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, ImU32 color) const override;

private:
    /// Whether the result on display shows the fuse open.
    bool m_IsBlown = false;
    /// Strength of the flash of the blow, from 0 (none) to 1 (the instant it blew).
    float m_FlashLevel = 0.0f;

    void DrawFlash(SchematicCanvas &canvas, const ViewTransform &view) const;
};

} // namespace GUI

#endif // IMCSIM_UI_FUSE_H
