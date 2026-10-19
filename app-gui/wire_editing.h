/**
 * @file    wire_editing.h
 * @brief   Keeps wires attached to terminals that move, moves selected wires, and cleans up the segments left behind.
 */

#ifndef IMCSIM_WIRE_EDITING_H
#define IMCSIM_WIRE_EDITING_H

#include "helpers.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <set>
#include <vector>

namespace GUI {

/**
 * @struct  PointTransform
 * @brief   A move of grid points that keeps wires horizontal or vertical: a translation, a quarter turn or a mirror
 *          about a point.
 */
struct PointTransform {
    /**
     * @enum    Kind
     * @brief   What the transform does to a point.
     * @details A quarter turn goes the way NextRotation() does. Mirrors swap left and right, or top and bottom,
     *          about the pivot.
     */
    enum class Kind {
        Translate,
        QuarterTurn,
        MirrorLeftRight,
        MirrorTopBottom
    };

    Kind Type = Kind::Translate;
    // How far a translation moves points
    GridPoint Delta;
    // The point the other kinds turn or mirror about
    GridPoint Pivot;

    GridPoint Apply(GridPoint point) const;
};

/**
 * @struct  MovedWires
 * @brief   The wires after a move, and which of them are the moved ones.
 */
struct MovedWires {
    std::vector<UIWire> Wires;
    std::set<std::size_t> Selected;
};

std::vector<UIWire> FollowTerminals(const std::vector<UIWire> &wires, const std::vector<GridPoint> &old_terminals,
                                    const std::vector<GridPoint> &new_terminals);
std::vector<UIWire> SimplifyWires(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals);
MovedWires MoveGroup(const std::vector<UIWire> &wires, const std::set<std::size_t> &selected,
                     const std::vector<GridPoint> &old_terminals, const std::vector<GridPoint> &new_terminals,
                     const std::vector<GridPoint> &fixed_terminals, const PointTransform &transform);

} // namespace GUI

#endif // IMCSIM_WIRE_EDITING_H
