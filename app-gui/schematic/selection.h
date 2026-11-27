/**
 * @file    selection.h
 * @brief   Finds which elements and wires a click, a selection box or a node picks, and combines picks into selections.
 */

#ifndef IMCSIM_SELECTION_H
#define IMCSIM_SELECTION_H

#include "imgui.h"
#include "schematic/connectivity.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <memory>
#include <set>
#include <vector>

namespace GUI {

/// How close, in pixels, the cursor must be to a wire to select it.
inline constexpr float WIRE_PICK_DISTANCE = 5.0f;

/**
 * @struct  Selection
 * @brief   Elements and wires picked, by index into the lists they came from.
 */
struct Selection {
    std::set<std::size_t> Elements;
    std::set<std::size_t> Wires;

    bool Empty() const;
    /**
     * @brief   Checks whether two Selection instances hold equal values.
     * @return  True when every field is equal.
     */
    bool operator==(const Selection &) const = default;
};

/**
 * @struct  IndexRange
 * @brief   Consecutive indices into a list, such as the items just added to it.
 */
struct IndexRange {
    std::size_t First = 0;
    std::size_t Count = 0;
};

/**
 * @struct  SelectedItems
 * @brief   The parts and wires a selection picks, in index order, for copying them out of the schematic.
 */
struct SelectedItems {
    std::vector<const UIElement *> Elements; ///< Parts of the schematic, which stay owned by it.
    std::vector<UIWire> Wires;
};

Selection MakeRangeSelection(IndexRange elements, IndexRange wires);
Selection PickAt(const std::vector<std::unique_ptr<UIElement>> &elements,
                 const std::vector<UIWire> &wires,
                 ImVec2 world_pos,
                 float wire_tolerance);
Selection PickInRectangle(const std::vector<std::unique_ptr<UIElement>> &elements,
                          const std::vector<UIWire> &wires,
                          ImVec2 corner,
                          ImVec2 opposite_corner);
Selection PickNode(const std::vector<UIWire> &wires, const Connectivity &connectivity, std::size_t wire_index);
Selection PickAll(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);
Selection Toggled(const Selection &current, const Selection &picked);
Selection Joined(const Selection &current, const Selection &picked);

// Selected items
SelectedItems CollectSelected(const std::vector<std::unique_ptr<UIElement>> &elements,
                              const std::vector<UIWire> &wires,
                              const Selection &selection);

} // namespace GUI

#endif // IMCSIM_SELECTION_H
