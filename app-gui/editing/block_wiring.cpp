/**
 * @file    block_wiring.cpp
 * @brief   Finds room for a block in the schematic and wires its terminals around its box.
 */

#include "editing/block_wiring.h"

#include "editing/wire_routing.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace GUI {

namespace {

/// How far from where it is a new block may move to find room, in grid steps.
constexpr int MAX_BLOCK_SHIFT = 20;

bool IsInside(const WorldBounds &bounds, const GridPoint point) {
    const auto x = static_cast<float>(point.X);
    const auto y = static_cast<float>(point.Y);
    return x >= bounds.Min.x && x <= bounds.Max.x && y >= bounds.Min.y && y <= bounds.Max.y;
}

/**
 * @brief   Whether a point lies on a horizontal or vertical wire, its ends included.
 */
bool IsOnWire(const UIWire &wire, const GridPoint point) {
    const GridPoint start = wire.GetStart();
    const GridPoint end = wire.GetEnd();
    return point.X >= std::min(start.X, end.X) && point.X <= std::max(start.X, end.X) &&
           point.Y >= std::min(start.Y, end.Y) && point.Y <= std::max(start.Y, end.Y) &&
           (start.X == end.X || start.Y == end.Y);
}

/**
 * @brief   The grid points of a horizontal or vertical wire that fall inside a box.
 */
std::vector<GridPoint> ListPointsInside(const UIWire &wire, const WorldBounds &bounds) {
    const GridPoint start = wire.GetStart();
    const GridPoint end = wire.GetEnd();
    const int low_x = std::max(std::min(start.X, end.X), static_cast<int>(std::ceil(bounds.Min.x)));
    const int high_x = std::min(std::max(start.X, end.X), static_cast<int>(std::floor(bounds.Max.x)));
    const int low_y = std::max(std::min(start.Y, end.Y), static_cast<int>(std::ceil(bounds.Min.y)));
    const int high_y = std::min(std::max(start.Y, end.Y), static_cast<int>(std::floor(bounds.Max.y)));
    std::vector<GridPoint> points;
    for (int y = low_y; y <= high_y; ++y) {
        for (int x = low_x; x <= high_x; ++x) {
            points.push_back({x, y});
        }
    }
    return points;
}

/**
 * @brief   Whether a block at its current spot leaves room for its wires.
 * @details Its box may cover no terminal and no wire, as a terminal on the middle of a wire would join them, and no
 *          point a pin is wired to. The exception is a pin wired from a point on its own lead, the usual case of
 *          markers where the terminals end up: the wire there, and what ends on it, lies over the lead.
 */
bool IsFreeSpot(const UIElement &block, const std::vector<GridPoint> &pin_points,
                const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires) {
    const WorldBounds bounds = block.GetWorldBounds();
    const std::vector<GridPoint> terminals = block.GetTerminals();
    // The lead of each pin whose point lies on it, or nothing
    std::vector<std::vector<GridPoint>> leads(pin_points.size());
    for (std::size_t pin = 0; pin < pin_points.size() && pin < terminals.size(); ++pin) {
        const GridPoint inward = block.GetTerminalInward(pin);
        const std::vector<GridPoint> lead = {terminals[pin],
                                             {terminals[pin].X + inward.X, terminals[pin].Y + inward.Y}};
        if (std::ranges::contains(lead, pin_points[pin])) {
            leads[pin] = lead;
        } else if (IsInside(bounds, pin_points[pin])) {
            return false;
        }
    }
    // A point of the box may hold something only on the lead of a pin, and only if it reaches that pin's point
    const auto is_on_own_lead = [&](const GridPoint point, const auto &reaches_pin) {
        for (std::size_t pin = 0; pin < leads.size(); ++pin) {
            if (std::ranges::contains(leads[pin], point) && reaches_pin(pin_points[pin])) {
                return true;
            }
        }
        return false;
    };
    for (const auto &element : elements) {
        for (const GridPoint terminal : element->GetTerminals()) {
            if (IsInside(bounds, terminal) &&
                !is_on_own_lead(terminal, [&](const GridPoint pin_point) { return pin_point == terminal; })) {
                return false;
            }
        }
    }
    return std::ranges::all_of(wires, [&](const UIWire &wire) {
        return std::ranges::all_of(ListPointsInside(wire, bounds), [&](const GridPoint point) {
            return is_on_own_lead(point, [&](const GridPoint pin_point) { return IsOnWire(wire, pin_point); });
        });
    });
}

/**
 * @brief   Where a wire from a point inside a box leaves it, or the point itself when it lies off the box; none when
 *          no wire on the point leaves the box.
 */
std::optional<GridPoint> FindWayOut(const std::vector<UIWire> &wires, const GridPoint point, const WorldBounds &box) {
    if (!IsInside(box, point)) {
        return point;
    }
    for (const UIWire &wire : wires) {
        const bool is_start_inside = IsInside(box, wire.GetStart());
        if (is_start_inside == IsInside(box, wire.GetEnd()) || !IsOnWire(wire, point)) {
            continue;
        }
        // Walk from the point toward the end outside until the box is left behind
        const GridPoint outside = is_start_inside ? wire.GetEnd() : wire.GetStart();
        const GridPoint step = {(outside.X > point.X) - (outside.X < point.X),
                                (outside.Y > point.Y) - (outside.Y < point.Y)};
        GridPoint way_out = point;
        while (IsInside(box, way_out)) {
            way_out = {way_out.X + step.X, way_out.Y + step.Y};
        }
        return way_out;
    }
    return std::nullopt;
}

/**
 * @brief   The wires with the one that runs from a point into a box cut back to the point, and the pieces inside the
 *          box that reach none of its terminals left out.
 */
std::vector<UIWire> CutAtBox(const std::vector<UIWire> &wires, const GridPoint point, const WorldBounds &box,
                             const std::vector<GridPoint> &terminals) {
    std::vector<UIWire> kept;
    for (const UIWire &wire : wires) {
        const bool is_start_inside = IsInside(box, wire.GetStart());
        const bool is_end_inside = IsInside(box, wire.GetEnd());
        if (is_start_inside && is_end_inside) {
            if (std::ranges::any_of(terminals, [&](const GridPoint terminal) { return IsOnWire(wire, terminal); })) {
                kept.push_back(wire);
            }
        } else if (is_start_inside == is_end_inside || !IsOnWire(wire, point)) {
            kept.push_back(wire);
        } else if (const GridPoint outside = is_start_inside ? wire.GetEnd() : wire.GetStart(); outside != point) {
            kept.emplace_back(outside, point);
        }
    }
    return kept;
}

} // namespace

/**
 * @brief   Moves a new block where its box leaves room for its wires, trying spots ever farther from where it is.
 * @param[in,out] block       The block, not among the elements yet, placed where it would best go.
 * @param[in]     pin_points  Where each of its pins is to be wired, in the order of its terminals.
 * @param[in]     elements    The other parts, whose terminals the box keeps off.
 * @param[in]     wires       The wires, which the box keeps off.
 * @note    The box may cover no terminal, wire or pin point, but for a pin point on the lead of its own pin; the block
 *          stays where it was when no spot within MAX_BLOCK_SHIFT steps is free.
 */
void MoveToFreeSpot(UIElement &block, const std::vector<GridPoint> &pin_points,
                    const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires) {
    const GridPoint middle = block.GetPosition();
    const auto is_free = [&](const GridPoint position) {
        block.SetPosition(position);
        return IsFreeSpot(block, pin_points, elements, wires);
    };
    for (int distance = 0; distance <= MAX_BLOCK_SHIFT; ++distance) {
        // The spots this many steps away along the grid
        for (int x_shift = -distance; x_shift <= distance; ++x_shift) {
            const int y_shift = distance - std::abs(x_shift);
            if (is_free({middle.X + x_shift, middle.Y - y_shift}) ||
                (y_shift != 0 && is_free({middle.X + x_shift, middle.Y + y_shift}))) {
                return;
            }
        }
    }
    block.SetPosition(middle);
}

/**
 * @brief   Frees the wires of a pin that moved to another edge of a block from where it was, so a new wire can go from
 *          them to the new edge around the box.
 * @param[in,out] wires      The wires: the one that came to where the pin was is cut back to where it leaves the box,
 *                           and the pieces left inside the box that reach none of its terminals go.
 * @param[in]     old_point  Where the pin's terminal was.
 * @param[in]     block      The block, as it is now.
 * @return  Where the new wire starts: the old point when it lies off the box, else where a wire on it leaves the box;
 *          none, with the wires unchanged, when no wire on it leaves the box.
 */
std::optional<GridPoint> DetachFromBox(std::vector<UIWire> &wires, const GridPoint old_point, const UIElement &block) {
    const WorldBounds box = block.GetWorldBounds();
    const std::optional<GridPoint> start = FindWayOut(wires, old_point, box);
    if (start) {
        wires = CutAtBox(wires, *start, box, block.GetTerminals());
    }
    return start;
}

/**
 * @brief   Routes new wires around some boxes and clear of the parts, the wires there and each other.
 * @param[in] elements  Every part, whose terminals the wires keep off but for the one each ends on.
 * @param[in] wires     The wires already there.
 * @param[in] boxes     Boxes no wire may cross, such as the bodies of the blocks the wires go to.
 * @param[in] links     Where each wire starts and ends.
 * @return  The wires of each link, in order, or none for a link that found no route.
 */
std::vector<std::optional<std::vector<UIWire>>> RouteLinks(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                           const std::vector<UIWire> &wires,
                                                           const std::vector<WorldBounds> &boxes,
                                                           const std::vector<WireLink> &links) {
    // Every start is kept off too, so no route runs over the point another one starts from
    std::vector<GridPoint> obstacles;
    for (const auto &element : elements) {
        std::ranges::copy(element->GetTerminals(), std::back_inserter(obstacles));
    }
    std::ranges::transform(links, std::back_inserter(obstacles), &WireLink::From);
    RoutingGrid grid(wires, obstacles);
    for (const WorldBounds &box : boxes) {
        grid.BlockArea({static_cast<int>(std::ceil(box.Min.x)), static_cast<int>(std::ceil(box.Min.y))},
                       {static_cast<int>(std::floor(box.Max.x)), static_cast<int>(std::floor(box.Max.y))});
    }
    std::vector<std::optional<std::vector<UIWire>>> routed;
    for (const WireLink &link : links) {
        const std::optional<std::vector<GridPoint>> path = grid.Route(link.From, link.To);
        if (!path) {
            routed.emplace_back();
            continue;
        }
        std::vector<UIWire> segments;
        for (std::size_t index = 1; index < path->size(); ++index) {
            segments.emplace_back((*path)[index - 1], (*path)[index]);
            grid.AddWire(segments.back());
        }
        routed.emplace_back(std::move(segments));
    }
    return routed;
}

} // namespace GUI
