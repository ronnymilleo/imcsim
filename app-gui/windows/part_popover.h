/**
 * @file    part_popover.h
 * @brief   The popover at the top of the editor canvas that holds the properties of the selected part.
 */

#ifndef IMCSIM_PART_POPOVER_H
#define IMCSIM_PART_POPOVER_H

#include "imgui.h"
#include "schematic/schematic.h"
#include "windows/part_editor.h"

namespace GUI {

/**
 * @class   PartPopover
 * @brief   Opens and draws the properties of the selected part in a popover over the canvas.
 * @details The fields are those of the PartEditor, which the Properties window draws too. The popover opens centered on
 *          the canvas, so it fits whatever part asked for it.
 */
class PartPopover {
public:
    // Canvas
    void SetCanvas(ImVec2 center, float top);

    // Opening
    void Place();
    void Request();
    void Open();
    void HandleValueTyping(PartEditor &part_editor);

    // Drawing
    void Draw(PartEditor &part_editor, Schematic &schematic);

private:
    /// The point the middle of the top edge of the popover opens on.
    ImVec2 m_Anchor = {0.0f, 0.0f};
    /// The middle of the canvas on screen, as drawn last.
    ImVec2 m_CanvasCenter = {0.0f, 0.0f};
    /// The top edge of the canvas on screen, as drawn last.
    float m_CanvasTop = 0.0f;
    /// Whether a menu or a key asked to open the popover.
    bool m_IsRequested = false;
};

} // namespace GUI

#endif // IMCSIM_PART_POPOVER_H
