/**
 * @file    canvas_pointer.h
 * @brief   Where the mouse is over the canvas, which every mode of the editor reads.
 */

#ifndef IMCSIM_CANVAS_POINTER_H
#define IMCSIM_CANVAS_POINTER_H

#include "base/helpers.h"
#include "imgui.h"

namespace GUI {

/**
 * @struct  CanvasPointer
 * @brief   The view of the canvas this frame and whether the mouse is over it, for the input of the modes.
 */
struct CanvasPointer {
    const ViewTransform &View;
    /// Pixels per grid unit.
    float Zoom;
    /// Whether the cursor is over the canvas, and no live part took the mouse where that matters.
    bool IsHovered;

    ImVec2 GetWorldPosition() const;
    float GetPickTolerance() const;
};

} // namespace GUI

#endif // IMCSIM_CANVAS_POINTER_H
