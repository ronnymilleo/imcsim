/**
 * @file    selection_editing.h
 * @brief   What the editor does to the selection of a schematic: replacing it, dragging it, turning it and clearing it.
 * @details The drag lives in the SelectingMode of the editor, so these take the mode along with the schematic; they do
 *          nothing to a drag while another mode is on.
 */

#ifndef IMCSIM_SELECTION_EDITING_H
#define IMCSIM_SELECTION_EDITING_H

#include "base/helpers.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "schematic/selection.h"
#include "schematic/wire_editing.h"
#include <optional>

namespace GUI {

// Dragging
bool IsDragging(const EditorMode &mode);
GroupDrag MakeDrag(const Schematic &schematic, GridPoint cursor, std::optional<Selection> collapse_to);
void UpdateDrag(Schematic &schematic, EditorMode &mode, GridPoint cursor);
void EndDrag(Schematic &schematic, EditorMode &mode);

// Selecting
void ApplySelection(Schematic &schematic, EditorMode &mode, const Selection &selection);
void ClearSelection(Schematic &schematic, EditorMode &mode);
void SelectAll(Schematic &schematic, EditorMode &mode);

// Turning
void TransformSelection(Schematic &schematic, EditorMode &mode, PointTransform::Kind kind, bool keep_wires);

} // namespace GUI

#endif // IMCSIM_SELECTION_EDITING_H
