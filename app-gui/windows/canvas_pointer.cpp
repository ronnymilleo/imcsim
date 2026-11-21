/**
 * @file    canvas_pointer.cpp
 * @brief   Where the mouse is over the canvas, which every mode of the editor reads.
 */

#include "windows/canvas_pointer.h"

#include "schematic/selection.h"

namespace GUI {

/**
 * @brief   Where the mouse is, in the units of the schematic.
 * @return  The position of the mouse this frame, unsnapped.
 */
ImVec2 CanvasPointer::GetWorldPosition() const {
    return View.ToWorld(ImGui::GetIO().MousePos);
}

/**
 * @brief   How far from a wire a click may land and still pick it, in the units of the schematic.
 * @return  The pick distance in pixels at the zoom of this frame.
 */
float CanvasPointer::GetPickTolerance() const {
    return WIRE_PICK_DISTANCE / Zoom;
}

} // namespace GUI
