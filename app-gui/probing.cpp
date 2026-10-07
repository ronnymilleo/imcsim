/**
 * @file    probing.cpp
 * @brief   Finds what the Probe tool measures at a point of the schematic: a node voltage or a current.
 */

#include "probing.h"

#include "simulator.h"
#include <cmath>
#include <format>

namespace GUI {

namespace {

// A transistor reports one current per terminal, so the terminal nearest the point picks it
std::size_t NearestTerminal(const UIElement &element, const ImVec2 world_pos) {
    const std::vector<GridPoint> terminals = element.GetTerminals();
    std::size_t nearest = 0;
    float nearest_distance = INFINITY;
    for (std::size_t index = 0; index < terminals.size(); ++index) {
        const ImVec2 offset = ToVec2(terminals[index]) - world_pos;
        const float distance = std::hypot(offset.x, offset.y);
        if (distance < nearest_distance) {
            nearest = index;
            nearest_distance = distance;
        }
    }
    return nearest;
}

} // namespace

/**
 * @brief   Finds what the Probe tool measures at a point, as the editor picks items: parts first, the most recent
 *          on top, then wires.
 * @param[in] elements        Parts of the schematic.
 * @param[in] wires           Wires of the schematic.
 * @param[in] connectivity    Nodes of the same parts and wires.
 * @param[in] world_pos       Point in world units, usually the cursor.
 * @param[in] wire_tolerance  How close to a wire the point must be, in world units.
 * @return  The current of a part (of the nearest terminal for a transistor), the voltage of the node of a wire, or
 *          no value over empty space, ground and wires on ground, which is always 0 V.
 */
std::optional<MeasurementTarget> FindMeasurementTarget(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                       const std::vector<UIWire> &wires,
                                                       const Connectivity &connectivity, const ImVec2 world_pos,
                                                       const float wire_tolerance) {
    for (std::size_t index = elements.size(); index-- > 0;) {
        const UIElement &element = *elements[index];
        if (!element.Contains(world_pos)) {
            continue;
        }
        const std::vector<std::string> currents = Core::GetCurrentNames(element.GetComponent());
        if (currents.empty()) {
            continue;
        }
        const std::size_t current = currents.size() == 1 ? 0 : NearestTerminal(element, world_pos);
        return MeasurementTarget{.Node = std::nullopt, .Current = currents[current]};
    }
    for (std::size_t index = wires.size(); index-- > 0;) {
        if (!wires[index].IsNear(world_pos, wire_tolerance)) {
            continue;
        }
        const std::optional<int> node = connectivity.GetNode(wires[index].GetStart());
        if (node && *node > 0) {
            return MeasurementTarget{.Node = node, .Current = {}};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

/**
 * @brief   Names a measurement as the plots do.
 * @param[in] target  A node voltage or a current.
 * @return  "V(3)" for the voltage of node 3, "I(R1)" for the current of R1.
 */
std::string GetMeasurementLabel(const MeasurementTarget &target) {
    if (target.Node) {
        return std::format("V({})", *target.Node);
    }
    return std::format("I({})", target.Current);
}

} // namespace GUI
