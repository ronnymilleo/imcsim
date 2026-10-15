/**
 * @file    wire_editing.cpp
 * @brief   Keeps wires attached to terminals that move and cleans up the segments left behind.
 */

#include "wire_editing.h"

#include <algorithm>
#include <optional>

namespace GUI {

namespace {

bool IsHorizontal(const UIWire &wire) {
    return wire.GetStart().Y == wire.GetEnd().Y;
}

bool IsStraight(const GridPoint start, const GridPoint end) {
    return start.X == end.X || start.Y == end.Y;
}

std::optional<GridPoint> FindMovedTerminal(const GridPoint point, const std::vector<GridPoint> &old_terminals,
                                           const std::vector<GridPoint> &new_terminals) {
    for (std::size_t index = 0; index < old_terminals.size(); ++index) {
        if (old_terminals[index] == point) {
            return new_terminals[index];
        }
    }
    return std::nullopt;
}

void AddSegment(std::vector<UIWire> &wires, const GridPoint start, const GridPoint end) {
    if (start != end) {
        wires.emplace_back(start, end);
    }
}

// The leg that reaches the terminal keeps the direction of the original wire, so it still enters the terminal
// the same way; the bend goes next to the fixed end
void AddBentWire(std::vector<UIWire> &wires, const GridPoint fixed, const GridPoint moved, const bool was_horizontal) {
    const GridPoint corner = was_horizontal ? GridPoint{fixed.X, moved.Y} : GridPoint{moved.X, fixed.Y};
    AddSegment(wires, fixed, corner);
    AddSegment(wires, corner, moved);
}

int CountWireEnds(const std::vector<UIWire> &wires, const GridPoint point) {
    return static_cast<int>(std::ranges::count_if(
        wires, [point](const UIWire &wire) { return wire.GetStart() == point || wire.GetEnd() == point; }));
}

GridPoint OtherEnd(const UIWire &wire, const GridPoint end) {
    return wire.GetStart() == end ? wire.GetEnd() : wire.GetStart();
}

// Two segments can become one when they meet end to end in a straight line at a point nothing else uses.
// Terminals are excluded so wires keep ending on them, and so do points another wire passes through, which
// would otherwise lose their connection
bool CanMergeAt(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals, const GridPoint point,
                const UIWire &first, const UIWire &second) {
    if (std::ranges::contains(terminals, point) || CountWireEnds(wires, point) != 2) {
        return false;
    }
    if (std::ranges::any_of(wires, [point](const UIWire &wire) { return wire.PassesThrough(point); })) {
        return false;
    }
    const GridPoint before = OtherEnd(first, point);
    const GridPoint after = OtherEnd(second, point);
    // Same line and on opposite sides of the shared point; a wire doubling back on itself is left alone
    if (before.Y == point.Y && after.Y == point.Y) {
        return (before.X < point.X) != (after.X < point.X);
    }
    if (before.X == point.X && after.X == point.X) {
        return (before.Y < point.Y) != (after.Y < point.Y);
    }
    return false;
}

bool IsSameWire(const UIWire &first, const UIWire &second) {
    return (first.GetStart() == second.GetStart() && first.GetEnd() == second.GetEnd()) ||
           (first.GetStart() == second.GetEnd() && first.GetEnd() == second.GetStart());
}

// Two wires on the same line that share some length, not just a point
bool Overlaps(const UIWire &first, const UIWire &second) {
    const bool horizontal = IsHorizontal(first);
    if (horizontal != IsHorizontal(second)) {
        return false;
    }
    const auto line = [horizontal](const UIWire &wire) { return horizontal ? wire.GetStart().Y : wire.GetStart().X; };
    const auto low = [horizontal](const UIWire &wire) {
        return horizontal ? std::min(wire.GetStart().X, wire.GetEnd().X) : std::min(wire.GetStart().Y, wire.GetEnd().Y);
    };
    const auto high = [horizontal](const UIWire &wire) {
        return horizontal ? std::max(wire.GetStart().X, wire.GetEnd().X) : std::max(wire.GetStart().Y, wire.GetEnd().Y);
    };
    return line(first) == line(second) && std::min(high(first), high(second)) > std::max(low(first), low(second));
}

bool TryMergeOnce(std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals) {
    for (std::size_t first = 0; first < wires.size(); ++first) {
        for (std::size_t second = first + 1; second < wires.size(); ++second) {
            for (const GridPoint point : {wires[first].GetStart(), wires[first].GetEnd()}) {
                const bool shares_point = wires[second].GetStart() == point || wires[second].GetEnd() == point;
                if (!shares_point || !CanMergeAt(wires, terminals, point, wires[first], wires[second])) {
                    continue;
                }
                const UIWire merged(OtherEnd(wires[first], point), OtherEnd(wires[second], point));
                wires.erase(wires.begin() + static_cast<std::ptrdiff_t>(second));
                wires[first] = merged;
                return true;
            }
        }
    }
    return false;
}

} // namespace

/**
 * @brief   Moves a grid point.
 * @param[in] point  Point to move.
 * @return  The point after the translation, the quarter turn or the mirror.
 */
GridPoint PointTransform::Apply(const GridPoint point) const {
    switch (Type) {
    case Kind::Translate:
        return point + Delta;
    case Kind::QuarterTurn:
        return Pivot + Rotate(point - Pivot, Rotation::R90);
    case Kind::MirrorLeftRight:
        return {2 * Pivot.X - point.X, point.Y};
    case Kind::MirrorTopBottom:
        return {point.X, 2 * Pivot.Y - point.Y};
    }
    return point;
}

/**
 * @brief   Moves wire ends that sat on terminals to where those terminals are now.
 * @param[in] wires          Wires before the terminals moved.
 * @param[in] old_terminals  Terminal positions before the move.
 * @param[in] new_terminals  Terminal positions after the move, in the same order.
 * @return  The updated wires. Segments that would turn diagonal are split into an L that bends next to their
 *          fixed end; segments that shrink to a point are dropped.
 * @note    Call it with the wires from before the drag started, not the previous frame, so moving back and
 *          forth does not pile up bends.
 */
std::vector<UIWire> FollowTerminals(const std::vector<UIWire> &wires, const std::vector<GridPoint> &old_terminals,
                                    const std::vector<GridPoint> &new_terminals) {
    std::vector<UIWire> result;
    result.reserve(wires.size());
    for (const UIWire &wire : wires) {
        const std::optional<GridPoint> moved_start = FindMovedTerminal(wire.GetStart(), old_terminals, new_terminals);
        const std::optional<GridPoint> moved_end = FindMovedTerminal(wire.GetEnd(), old_terminals, new_terminals);
        const GridPoint start = moved_start.value_or(wire.GetStart());
        const GridPoint end = moved_end.value_or(wire.GetEnd());

        if (IsStraight(start, end)) {
            AddSegment(result, start, end);
        } else if (moved_end) {
            AddBentWire(result, start, end, IsHorizontal(wire));
        } else {
            AddBentWire(result, end, start, IsHorizontal(wire));
        }
    }
    return result;
}

/**
 * @brief   Removes duplicate segments and joins straight runs split by a point nothing else uses.
 * @param[in] wires      Wires to clean up.
 * @param[in] terminals  Every terminal of the schematic; wires are never merged across them.
 * @return  An electrically equivalent set of wires with fewer segments.
 */
std::vector<UIWire> SimplifyWires(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals) {
    std::vector<UIWire> result;
    for (const UIWire &wire : wires) {
        const bool duplicate =
            std::ranges::any_of(result, [&wire](const UIWire &kept) { return IsSameWire(kept, wire); });
        if (!duplicate && wire.GetStart() != wire.GetEnd()) {
            result.push_back(wire);
        }
    }
    while (TryMergeOnce(result, terminals)) {
    }
    return result;
}

/**
 * @brief   Moves a group of wires and parts, and keeps everything else attached to them.
 * @param[in] wires            Wires before the move.
 * @param[in] selected         Indices of the wires that move.
 * @param[in] old_terminals    Terminals of the parts that move, before the move.
 * @param[in] new_terminals    The same terminals after the move, in the same order.
 * @param[in] fixed_terminals  Terminals of the parts that stay.
 * @param[in] transform        What happens to the selected wires.
 * @return  The wires after the move and which of them are the moved ones. Wires that are not selected and end on
 *          something that moved follow it, as FollowTerminals() does, and a moved wire that ended on a part that
 *          stays, or on the middle of a wire that stays, leaves a wire behind that keeps it connected, and so does a
 *          part that stays and touches a moved wire in its middle.
 * @note    Call it with the wires from before the move started, not the previous frame, so moving back and forth
 *          does not pile up bends.
 */
MovedWires MoveGroup(const std::vector<UIWire> &wires, const std::set<std::size_t> &selected,
                     const std::vector<GridPoint> &old_terminals, const std::vector<GridPoint> &new_terminals,
                     const std::vector<GridPoint> &fixed_terminals, const PointTransform &transform) {
    std::vector<UIWire> staying;
    std::vector<UIWire> carried;
    for (std::size_t index = 0; index < wires.size(); ++index) {
        (selected.contains(index) ? carried : staying).push_back(wires[index]);
    }

    // Wires that stay follow every point that moves: ends of the moved wires, and ends of wires that touch them in
    // the middle. A part that stays keeps its terminals where they are
    std::vector<GridPoint> from = old_terminals;
    std::vector<GridPoint> to = new_terminals;
    const auto follow = [&](const GridPoint point) {
        if (!std::ranges::contains(fixed_terminals, point) && !std::ranges::contains(from, point)) {
            from.push_back(point);
            to.push_back(transform.Apply(point));
        }
    };
    for (const UIWire &wire : carried) {
        follow(wire.GetStart());
        follow(wire.GetEnd());
        for (const UIWire &other : staying) {
            for (const GridPoint end : {other.GetStart(), other.GetEnd()}) {
                if (wire.PassesThrough(end)) {
                    follow(end);
                }
            }
        }
    }
    std::vector<UIWire> result = FollowTerminals(staying, from, to);

    std::vector<UIWire> moved;
    for (const UIWire &wire : carried) {
        const UIWire shifted(transform.Apply(wire.GetStart()), transform.Apply(wire.GetEnd()));
        moved.push_back(shifted);
        for (const GridPoint end : {wire.GetStart(), wire.GetEnd()}) {
            const bool on_fixed_part = std::ranges::contains(fixed_terminals, end);
            const bool on_staying_wire =
                std::ranges::any_of(staying, [end](const UIWire &other) { return other.PassesThrough(end); });
            if (on_fixed_part || on_staying_wire) {
                AddBentWire(result, end, transform.Apply(end), IsHorizontal(shifted));
            }
        }
        // A part that stays and sat on the middle of the wire, such as a ground on a rail, stays connected to it
        for (const GridPoint terminal : fixed_terminals) {
            if (wire.PassesThrough(terminal)) {
                AddBentWire(result, terminal, transform.Apply(terminal), IsHorizontal(shifted));
            }
        }
    }
    result.insert(result.end(), moved.begin(), moved.end());

    std::vector<GridPoint> terminals = fixed_terminals;
    terminals.insert(terminals.end(), new_terminals.begin(), new_terminals.end());
    MovedWires moved_wires;
    moved_wires.Wires = SimplifyWires(result, terminals);
    for (std::size_t index = 0; index < moved_wires.Wires.size(); ++index) {
        if (std::ranges::any_of(moved, [&](const UIWire &wire) { return Overlaps(wire, moved_wires.Wires[index]); })) {
            moved_wires.Selected.insert(index);
        }
    }
    return moved_wires;
}

} // namespace GUI
