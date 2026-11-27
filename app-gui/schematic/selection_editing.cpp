/**
 * @file    selection_editing.cpp
 * @brief   What the editor does to the selection of a schematic: replacing it, dragging it, turning it and clearing it.
 */

#include "schematic/selection_editing.h"

#include "schematic/group_editing.h"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace GUI {

namespace {

/**
 * @brief   The terminals of some parts, in the order of the parts.
 */
std::vector<GridPoint> ListTerminals(const std::vector<std::unique_ptr<UIElement>> &elements,
                                     const std::set<std::size_t> &indices) {
    std::vector<GridPoint> terminals;
    for (const std::size_t index : indices) {
        std::ranges::copy(elements[index]->GetTerminals(), std::back_inserter(terminals));
    }
    return terminals;
}

/**
 * @brief   The terminals of a move of some parts, before it: where those of the parts that move are, and those of the
 *          parts that stay; the new terminals are left for after the move.
 */
TerminalMove StartTerminalMove(const std::vector<std::unique_ptr<UIElement>> &elements,
                               const std::set<std::size_t> &moved) {
    TerminalMove terminals;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        std::ranges::copy(elements[index]->GetTerminals(),
                          std::back_inserter(moved.contains(index) ? terminals.Old : terminals.Fixed));
    }
    return terminals;
}

/**
 * @brief   Puts the dragged parts where they started, moved by the delta.
 */
void MoveDraggedElements(Schematic &schematic, const GroupDrag &drag, const GridPoint delta) {
    std::size_t position_index = 0;
    for (const std::size_t index : drag.Elements) {
        schematic.GetElement(index).SetPosition(drag.StartPositions[position_index++] + delta);
    }
}

} // namespace

/**
 * @brief   A drag keeps the positions of the items it moves, so deleting, cutting or undoing waits until it ends.
 * @param[in] mode  Editor mode to check.
 * @return  True when the mode is selecting with an active drag gesture.
 */
bool IsDragging(const EditorMode &mode) {
    const auto *selecting = std::get_if<SelectingMode>(&mode);
    return selecting != nullptr && selecting->Drag.has_value();
}

/**
 * @brief   Captures what a drag of the current selection needs, so every frame can be computed from the start.
 * @details A click on one item of a group only keeps the group while the mouse moves: without a move it leaves just
 *          that item selected.
 * @param[in] schematic    Schematic whose current selection is starting to move.
 * @param[in] cursor       Grid position where the drag started.
 * @param[in] collapse_to  Selection to fall back to when the mouse is released without moving, or none.
 * @return  Initial drag state for the current selection.
 */
GroupDrag MakeDrag(const Schematic &schematic, const GridPoint cursor, std::optional<Selection> collapse_to) {
    GroupDrag drag;
    drag.StartCursor = cursor;
    drag.Elements = schematic.GetSelection().Elements;
    drag.Wires = schematic.GetSelection().Wires;
    drag.StartWires = schematic.GetWires();
    drag.CollapseTo = std::move(collapse_to);
    const auto &elements = schematic.GetElements();
    for (const std::size_t index : drag.Elements) {
        drag.StartPositions.push_back(elements[index]->GetPosition());
    }
    drag.StartTerminals = StartTerminalMove(elements, drag.Elements);
    if (drag.Elements.empty() && drag.Wires.size() == 1) {
        const UIWire &wire = drag.StartWires[*drag.Wires.begin()];
        drag.SlidesVertically = wire.GetStart().Y == wire.GetEnd().Y;
    }
    return drag;
}

/**
 * @brief   Moves the selection by how far the cursor went from where the drag started, rebuilding the wires from the
 *          wires the drag started with so moving back and forth does not pile up bends.
 * @param[in,out] schematic  Schematic whose selected elements and attached wires are moved.
 * @param[in,out] mode       Editor mode holding the active drag state.
 * @param[in]     cursor     Current snapped grid position of the cursor.
 * @note    It does nothing unless the mode is selecting with a drag in progress.
 */
void UpdateDrag(Schematic &schematic, EditorMode &mode, const GridPoint cursor) {
    auto *selecting = std::get_if<SelectingMode>(&mode);
    if (selecting == nullptr || !selecting->Drag) {
        return;
    }
    GroupDrag &drag = *selecting->Drag;
    drag.LastCursor = cursor;
    GridPoint delta = cursor - drag.StartCursor;
    if (drag.SlidesVertically) {
        // A wire only slides across itself: along its own line, moving it would change nothing
        (*drag.SlidesVertically ? delta.X : delta.Y) = 0;
    }
    if (delta == drag.LastDelta) {
        return;
    }
    drag.LastDelta = delta;
    drag.Moved = delta != GridPoint{};
    if (drag.Moved) {
        drag.CollapseTo.reset();
    }

    MoveDraggedElements(schematic, drag, delta);
    TerminalMove terminals = drag.StartTerminals;
    terminals.New = ListTerminals(schematic.GetElements(), drag.Elements);
    const PointTransform translation = {.Type = PointTransform::Kind::TRANSLATE, .Delta = delta, .Pivot = {}};
    MovedWires moved = MoveGroup(drag.StartWires, drag.Wires, terminals, translation);
    schematic.SetWires(std::move(moved.Wires));
    schematic.SetSelection({.Elements = drag.Elements, .Wires = std::move(moved.Selected)});
    schematic.MarkModified();
}

/**
 * @brief   What the drag left selected: just the clicked item when the mouse never moved over a group.
 * @details The cleanup of the wires is done by every frame of the drag already, so it only matters when the drag
 *          changed nothing.
 * @param[in,out] schematic  Schematic whose selection and wires are finalized.
 * @param[in,out] mode       Editor mode whose active drag is cleared.
 */
void EndDrag(Schematic &schematic, EditorMode &mode) {
    auto *selecting = std::get_if<SelectingMode>(&mode);
    if (selecting == nullptr || !selecting->Drag) {
        return;
    }
    const std::optional<Selection> collapse_to = std::move(selecting->Drag->CollapseTo);
    selecting->Drag.reset();
    if (collapse_to) {
        schematic.SetSelection(*collapse_to);
    }
    schematic.SimplifyAllWires();
}

/**
 * @brief   Replaces the selection; a drag belongs to the items it was started with, so it ends.
 * @param[in,out] schematic  Schematic whose selection is replaced.
 * @param[in,out] mode       Editor mode whose active drag is ended first.
 * @param[in]     selection  New selection to set.
 */
void ApplySelection(Schematic &schematic, EditorMode &mode, const Selection &selection) {
    EndDrag(schematic, mode);
    schematic.SetSelection(selection);
}

/**
 * @brief   Deselects everything, as Esc does, and ends what was being done with the selection: a drag belongs to the
 *          selection, so it ends with it, and so does a selection box.
 * @details Schematic::ClearSelection() only deselects; this is the one the editor calls.
 * @param[in,out] schematic  Schematic whose selection is cleared.
 * @param[in,out] mode       Editor mode whose active drag and selection box are cleared.
 */
void CancelSelection(Schematic &schematic, EditorMode &mode) {
    EndDrag(schematic, mode);
    if (auto *selecting = std::get_if<SelectingMode>(&mode)) {
        selecting->Box.reset();
    }
    schematic.ClearSelection();
}

/**
 * @brief   Selects every part and wire, while the editor is selecting.
 * @param[in,out] schematic  Schematic whose parts and wires are selected.
 * @param[in,out] mode       Editor mode; must be selecting for the call to take effect.
 */
void SelectAll(Schematic &schematic, EditorMode &mode) {
    if (!IsSelecting(mode)) {
        return;
    }
    ApplySelection(schematic, mode, PickAll(schematic.GetElements(), schematic.GetWires()));
}

/**
 * @brief   Turns or mirrors every selected part about the center of the selection, and the selected wires with them.
 * @details The wires that meet them follow, unless keep_wires: then the parts turn alone and every wire stays where it
 *          was. A turn during a drag ends that move and goes on dragging from the new position.
 * @param[in,out] schematic   Schematic whose selected parts and wires are transformed.
 * @param[in,out] mode        Editor mode; an active drag is restarted from the transformed positions.
 * @param[in]     kind        Quarter turn or mirror transform to apply.
 * @param[in]     keep_wires  True to leave all wires untouched while turning only the selected parts.
 */
void TransformSelection(Schematic &schematic,
                        EditorMode &mode,
                        const PointTransform::Kind kind,
                        const bool keep_wires) {
    if (schematic.GetSelection().Elements.empty()) {
        return;
    }
    const auto *selecting = std::get_if<SelectingMode>(&mode);
    const std::optional<GridPoint> drag_cursor =
        selecting != nullptr && selecting->Drag ? std::optional(selecting->Drag->LastCursor) : std::nullopt;
    EndDrag(schematic, mode);

    const auto &elements = schematic.GetElements();
    const Selection selected = schematic.GetSelection();
    const PointTransform transform = {.Type = kind,
                                      .Delta = {},
                                      .Pivot = FindPivot(elements, schematic.GetWires(), selected)};

    TerminalMove terminals = StartTerminalMove(elements, selected.Elements);
    for (const std::size_t index : selected.Elements) {
        TransformElement(schematic.GetElement(index), transform);
    }
    terminals.New = ListTerminals(elements, selected.Elements);
    if (!keep_wires) {
        MovedWires moved = MoveGroup(schematic.GetWires(), selected.Wires, terminals, transform);
        schematic.SetWires(std::move(moved.Wires));
        schematic.SetSelection({.Elements = selected.Elements, .Wires = std::move(moved.Selected)});
    }
    schematic.MarkModified();
    if (drag_cursor) {
        std::get<SelectingMode>(mode).Drag = MakeDrag(schematic, *drag_cursor, std::nullopt);
    }
}

} // namespace GUI
