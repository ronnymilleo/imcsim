/**
 * @file    selection_editing.cpp
 * @brief   What the editor does to the selection of a schematic: replacing it, dragging it, turning it and clearing it.
 */

#include "schematic/selection_editing.h"

#include "schematic/group_editing.h"
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace GUI {

/**
 * @brief   A drag keeps the positions of the items it moves, so deleting, cutting or undoing waits until it ends.
 */
bool IsDragging(const EditorMode &mode) {
    const auto *selecting = std::get_if<SelectingMode>(&mode);
    return selecting != nullptr && selecting->Drag.has_value();
}

/**
 * @brief   Captures what a drag of the current selection needs, so every frame can be computed from the start.
 * @details A click on one item of a group only keeps the group while the mouse moves: without a move it leaves just
 *          that item selected.
 */
GroupDrag MakeDrag(const Schematic &schematic, const GridPoint cursor, std::optional<Selection> collapse_to) {
    GroupDrag drag;
    drag.StartCursor = cursor;
    drag.Elements = schematic.GetSelectedElements();
    drag.Wires = schematic.GetSelectedWires();
    drag.StartWires = schematic.GetWires();
    drag.CollapseTo = std::move(collapse_to);
    const auto &elements = schematic.GetElements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> terminals = elements[index]->GetTerminals();
        if (drag.Elements.contains(index)) {
            drag.StartPositions.push_back(elements[index]->GetPosition());
            drag.StartTerminals.insert(drag.StartTerminals.end(), terminals.begin(), terminals.end());
        } else {
            drag.FixedTerminals.insert(drag.FixedTerminals.end(), terminals.begin(), terminals.end());
        }
    }
    if (drag.Elements.empty() && drag.Wires.size() == 1) {
        const UIWire &wire = drag.StartWires[*drag.Wires.begin()];
        drag.SlidesVertically = wire.GetStart().Y == wire.GetEnd().Y;
    }
    return drag;
}

/**
 * @brief   Moves the selection by how far the cursor went from where the drag started, rebuilding the wires from the
 *          wires the drag started with so moving back and forth does not pile up bends.
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

    std::vector<GridPoint> new_terminals;
    std::size_t position_index = 0;
    for (const std::size_t index : drag.Elements) {
        UIElement &element = schematic.GetElement(index);
        element.SetPosition(drag.StartPositions[position_index++] + delta);
        const std::vector<GridPoint> terminals = element.GetTerminals();
        new_terminals.insert(new_terminals.end(), terminals.begin(), terminals.end());
    }
    const PointTransform translation = {.Type = PointTransform::Kind::TRANSLATE, .Delta = delta, .Pivot = {}};
    MovedWires moved =
        MoveGroup(drag.StartWires, drag.Wires, drag.StartTerminals, new_terminals, drag.FixedTerminals, translation);
    schematic.SetWires(std::move(moved.Wires));
    schematic.SetSelection(drag.Elements, std::move(moved.Selected));
    schematic.MarkModified();
}

/**
 * @brief   What the drag left selected: just the clicked item when the mouse never moved over a group.
 * @details The cleanup of the wires is done by every frame of the drag already, so it only matters when the drag
 *          changed nothing.
 */
void EndDrag(Schematic &schematic, EditorMode &mode) {
    auto *selecting = std::get_if<SelectingMode>(&mode);
    if (selecting == nullptr || !selecting->Drag) {
        return;
    }
    const std::optional<Selection> collapse_to = std::move(selecting->Drag->CollapseTo);
    selecting->Drag.reset();
    if (collapse_to) {
        schematic.SetSelection(collapse_to->Elements, collapse_to->Wires);
    }
    schematic.SimplifyAllWires();
}

/**
 * @brief   Replaces the selection; a drag belongs to the items it was started with, so it ends.
 */
void ApplySelection(Schematic &schematic, EditorMode &mode, const Selection &selection) {
    EndDrag(schematic, mode);
    schematic.SetSelection(selection.Elements, selection.Wires);
}

/**
 * @brief   A drag belongs to the selection, so it ends with it, and so does a selection box.
 */
void ClearSelection(Schematic &schematic, EditorMode &mode) {
    EndDrag(schematic, mode);
    if (auto *selecting = std::get_if<SelectingMode>(&mode)) {
        selecting->Box.reset();
    }
    schematic.ClearSelection();
}

/**
 * @brief   Selects every part and wire, while the editor is selecting.
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
 */
void TransformSelection(Schematic &schematic, EditorMode &mode, const PointTransform::Kind kind,
                        const bool keep_wires) {
    if (schematic.GetSelectedElements().empty()) {
        return;
    }
    const auto *selecting = std::get_if<SelectingMode>(&mode);
    const std::optional<GridPoint> drag_cursor =
        selecting != nullptr && selecting->Drag ? std::optional(selecting->Drag->LastCursor) : std::nullopt;
    EndDrag(schematic, mode);

    const auto &elements = schematic.GetElements();
    const std::set<std::size_t> selected_elements = schematic.GetSelectedElements();
    const std::set<std::size_t> selected_wires = schematic.GetSelectedWires();
    const PointTransform transform = {.Type = kind,
                                      .Delta = {},
                                      .Pivot =
                                          FindPivot(elements, selected_elements, schematic.GetWires(), selected_wires)};

    std::vector<GridPoint> old_terminals;
    std::vector<GridPoint> new_terminals;
    std::vector<GridPoint> fixed_terminals;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::vector<GridPoint> before = elements[index]->GetTerminals();
        if (!selected_elements.contains(index)) {
            fixed_terminals.insert(fixed_terminals.end(), before.begin(), before.end());
            continue;
        }
        old_terminals.insert(old_terminals.end(), before.begin(), before.end());
        TransformElement(schematic.GetElement(index), transform);
        const std::vector<GridPoint> after = elements[index]->GetTerminals();
        new_terminals.insert(new_terminals.end(), after.begin(), after.end());
    }
    if (!keep_wires) {
        MovedWires moved =
            MoveGroup(schematic.GetWires(), selected_wires, old_terminals, new_terminals, fixed_terminals, transform);
        schematic.SetWires(std::move(moved.Wires));
        schematic.SetSelection(selected_elements, std::move(moved.Selected));
    }
    schematic.MarkModified();
    if (drag_cursor) {
        std::get<SelectingMode>(mode).Drag = MakeDrag(schematic, *drag_cursor, std::nullopt);
    }
}

} // namespace GUI
