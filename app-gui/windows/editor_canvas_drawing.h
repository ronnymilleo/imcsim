/**
 * @file    editor_canvas_drawing.h
 * @brief   What the editor draws on its canvas besides the schematic: the grid, the selection box and the shown
 * instant.
 */

#ifndef IMCSIM_EDITOR_CANVAS_DRAWING_H
#define IMCSIM_EDITOR_CANVAS_DRAWING_H

#include "base/helpers.h"
#include "imgui.h"
#include "schematic/editor_mode.h"
#include <optional>

namespace GUI {

// Background
void DrawGrid(ImDrawList *draw_list, const ViewTransform &view, ImVec2 origin, ImVec2 size, float zoom);

// Overlays
void DrawSelectionBox(ImDrawList *draw_list, const ViewTransform &view, const EditorMode &mode);
void DrawShownInstant(ImDrawList *draw_list, ImVec2 origin, std::optional<double> time);

} // namespace GUI

#endif // IMCSIM_EDITOR_CANVAS_DRAWING_H
