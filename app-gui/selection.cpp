/**
 * @file    selection.cpp
 * @brief   Finds which elements and wires a click, a selection box or a node picks, and combines picks into selections.
 */

#include "selection.h"

#include <algorithm>

namespace GUI {

namespace {

// Wires are horizontal or vertical, so a wire is its own bounding box
bool WireIntersects(const UIWire &wire, const ImVec2 min, const ImVec2 max) {
    const GridPoint start = wire.GetStart();
    const GridPoint end = wire.GetEnd();
    return static_cast<float>(std::min(start.X, end.X)) <= max.x &&
           static_cast<float>(std::max(start.X, end.X)) >= min.x &&
           static_cast<float>(std::min(start.Y, end.Y)) <= max.y &&
           static_cast<float>(std::max(start.Y, end.Y)) >= min.y;
}

} // namespace

/**
 * @brief   Tells whether nothing is picked.
 * @return  True when there are no elements and no wires.
 */
bool Selection::Empty() const {
    return Elements.empty() && Wires.empty();
}

/**
 * @brief   Picks what is under a point: one element or, when there is none, one wire.
 * @param[in] elements        Parts of the schematic.
 * @param[in] wires           Wires of the schematic.
 * @param[in] world_pos       Point in world units, usually the cursor.
 * @param[in] wire_tolerance  How close, in world units, the point must be to a wire.
 * @return  Elements win over wires, and the most recently added item wins because it is drawn on top. Empty when
 *          nothing is there.
 */
Selection PickAt(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                 const ImVec2 world_pos, const float wire_tolerance) {
    for (std::size_t index = elements.size(); index-- > 0;) {
        if (elements[index]->Contains(world_pos)) {
            return {.Elements = {index}, .Wires = {}};
        }
    }
    for (std::size_t index = wires.size(); index-- > 0;) {
        if (wires[index].IsNear(world_pos, wire_tolerance)) {
            return {.Elements = {}, .Wires = {index}};
        }
    }
    return {};
}

/**
 * @brief   Picks everything a selection box touches.
 * @param[in] elements         Parts of the schematic.
 * @param[in] wires            Wires of the schematic.
 * @param[in] corner           One corner of the box, in world units.
 * @param[in] opposite_corner  The other corner, in any position relative to the first.
 * @return  Every element whose symbol bounds and every wire that overlaps the box.
 */
Selection PickInRectangle(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          const ImVec2 corner, const ImVec2 opposite_corner) {
    const ImVec2 min = {std::min(corner.x, opposite_corner.x), std::min(corner.y, opposite_corner.y)};
    const ImVec2 max = {std::max(corner.x, opposite_corner.x), std::max(corner.y, opposite_corner.y)};
    Selection picked;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index]->Intersects(min, max)) {
            picked.Elements.insert(index);
        }
    }
    for (std::size_t index = 0; index < wires.size(); ++index) {
        if (WireIntersects(wires[index], min, max)) {
            picked.Wires.insert(index);
        }
    }
    return picked;
}

/**
 * @brief   Picks every wire of the node a wire belongs to.
 * @param[in] wires         Wires of the schematic.
 * @param[in] connectivity  Nodes of the schematic.
 * @param[in] wire_index    Index of a wire of the node.
 * @return  The wires joined to it, itself included; just the wire for an index that is out of range.
 */
Selection PickNode(const std::vector<UIWire> &wires, const Connectivity &connectivity, const std::size_t wire_index) {
    Selection picked;
    if (wire_index >= wires.size()) {
        return picked;
    }
    const std::optional<int> node = connectivity.GetNode(wires[wire_index].GetStart());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        if (index == wire_index || (node && connectivity.GetNode(wires[index].GetStart()) == node)) {
            picked.Wires.insert(index);
        }
    }
    return picked;
}

/**
 * @brief   Picks every element and every wire.
 * @param[in] elements  Parts of the schematic.
 * @param[in] wires     Wires of the schematic.
 * @return  All of them.
 */
Selection PickAll(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires) {
    Selection picked;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        picked.Elements.insert(index);
    }
    for (std::size_t index = 0; index < wires.size(); ++index) {
        picked.Wires.insert(index);
    }
    return picked;
}

/**
 * @brief   Flips the selection of every picked item, as a Shift+click does.
 * @param[in] current  The selection so far.
 * @param[in] picked   Items to flip: selected ones leave the selection and the others join it.
 * @return  The new selection.
 */
Selection Toggled(const Selection &current, const Selection &picked) {
    Selection result = current;
    const auto toggle = [](std::set<std::size_t> &target, const std::set<std::size_t> &items) {
        for (const std::size_t item : items) {
            if (!target.erase(item)) {
                target.insert(item);
            }
        }
    };
    toggle(result.Elements, picked.Elements);
    toggle(result.Wires, picked.Wires);
    return result;
}

/**
 * @brief   Adds the picked items to the selection, as a Shift+drag selection box does.
 * @param[in] current  The selection so far.
 * @param[in] picked   Items to add.
 * @return  The new selection.
 */
Selection Joined(const Selection &current, const Selection &picked) {
    Selection result = current;
    result.Elements.insert(picked.Elements.begin(), picked.Elements.end());
    result.Wires.insert(picked.Wires.begin(), picked.Wires.end());
    return result;
}

} // namespace GUI
