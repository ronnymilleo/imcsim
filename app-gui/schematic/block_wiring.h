/**
 * @file    block_wiring.h
 * @brief   Finds room for a block in the schematic and wires its terminals around its box.
 */

#ifndef IMCSIM_BLOCK_WIRING_H
#define IMCSIM_BLOCK_WIRING_H

#include "base/helpers.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_subcircuit.h"
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

/**
 * @struct  RoutingObstacles
 * @brief   What new wires go around besides the parts and wires: the blocks they go to, with their names.
 */
struct RoutingObstacles {
    /// Boxes no wire may cross, such as the bodies of the blocks the wires go to.
    std::vector<WorldBounds> Boxes;
    /// Single points no wire may cross, such as those under the names of the blocks.
    std::vector<GridPoint> Points;
};

// Placement
void MoveToFreeSpot(UISubcircuit &block, const std::vector<GridPoint> &pin_points,
                    const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires);
std::optional<GridPoint> DetachFromBox(std::vector<UIWire> &wires, GridPoint old_point, const UIElement &block);

// Routing
void KeepOffBlock(RoutingObstacles &obstacles, const UISubcircuit &block);
std::vector<std::optional<std::vector<UIWire>>> RouteLinks(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                           const std::vector<UIWire> &wires,
                                                           const RoutingObstacles &obstacles,
                                                           const std::vector<WireLink> &links);

} // namespace GUI

#endif // IMCSIM_BLOCK_WIRING_H
