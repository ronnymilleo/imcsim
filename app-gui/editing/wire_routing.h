/**
 * @file    wire_routing.h
 * @brief   Finds a path on the grid for a new wire that keeps clear of parts, terminals and the wires already there.
 */

#ifndef IMCSIM_WIRE_ROUTING_H
#define IMCSIM_WIRE_ROUTING_H

#include "helpers.h"
#include "ui_elements/ui_wire.h"
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   RoutingGrid
 * @brief   The grid points a new wire may use, and the shortest path with the fewest bends between two of them.
 * @details A wire may not touch a terminal, the end of another wire or a blocked area, nor run along another wire,
 *          since any of those would join nets that are apart. It may cross another wire, as crossing wires do not
 *          connect, but never bend on one, which would make a T-junction.
 */
class RoutingGrid {
public:
    RoutingGrid(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals);

    // Obstacles
    void BlockArea(GridPoint min, GridPoint max);
    void AddWire(const UIWire &wire);

    // Routing
    [[nodiscard]] std::optional<std::vector<GridPoint>> Route(GridPoint from, GridPoint to) const;

private:
    /**
     * @enum    Use
     * @brief   What lies on a grid point: the middle of a horizontal or of a vertical wire, which a new wire may cross
     *          at a right angle, or anything it must keep off.
     */
    enum class Use : std::uint8_t {
        HORIZONTAL,
        VERTICAL,
        BLOCKED
    };

    /**
     * @struct  Area
     * @brief   A blocked rectangle, corners included, which a route may have to go around.
     */
    struct Area {
        GridPoint Min;
        GridPoint Max;
    };

    /// The points that are not free; every other point is.
    std::map<GridPoint, Use> m_Uses;
    std::vector<Area> m_Areas;

    void Mark(GridPoint point, Use use);
};

} // namespace GUI

#endif // IMCSIM_WIRE_ROUTING_H
