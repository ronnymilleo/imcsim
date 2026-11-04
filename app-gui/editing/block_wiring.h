/**
 * @file    block_wiring.h
 * @brief   Finds room for a block in the schematic and wires its terminals around its box.
 */

#ifndef IMCSIM_BLOCK_WIRING_H
#define IMCSIM_BLOCK_WIRING_H

#include "helpers.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  WireLink
 * @brief   A wire to draw from a point to a terminal.
 */
struct WireLink {
    GridPoint From;
    GridPoint To;
};

// Placement
void MoveToFreeSpot(UIElement &block, const std::vector<GridPoint> &pin_points,
                    const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);
std::optional<GridPoint> DetachFromBox(std::vector<UIWire> &wires, GridPoint old_point, const UIElement &block);

// Routing
std::vector<std::optional<std::vector<UIWire>>> RouteLinks(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                           const std::vector<UIWire> &wires,
                                                           const std::vector<WorldBounds> &boxes,
                                                           const std::vector<WireLink> &links);

} // namespace GUI

#endif // IMCSIM_BLOCK_WIRING_H
