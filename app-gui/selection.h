/**
 * @file    selection.h
 * @brief   Finds which elements and wires a click, a selection box or a node picks, and combines picks into selections.
 */

#ifndef IMCSIM_SELECTION_H
#define IMCSIM_SELECTION_H

#include "connectivity.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <memory>
#include <set>
#include <vector>

namespace GUI {

/**
 * @struct  Selection
 * @brief   Elements and wires picked, by index into the lists they came from.
 */
struct Selection {
    std::set<std::size_t> Elements;
    std::set<std::size_t> Wires;

    bool Empty() const;
    bool operator==(const Selection &) const = default;
};

Selection PickAt(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                 ImVec2 world_pos, float wire_tolerance);
Selection PickInRectangle(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                          ImVec2 corner, ImVec2 opposite_corner);
Selection PickNode(const std::vector<UIWire> &wires, const Connectivity &connectivity, std::size_t wire_index);
Selection PickAll(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);
Selection Toggled(const Selection &current, const Selection &picked);
Selection Joined(const Selection &current, const Selection &picked);

} // namespace GUI

#endif // IMCSIM_SELECTION_H
