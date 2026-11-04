/**
 * @file    wire_routing.cpp
 * @brief   Finds a path on the grid for a new wire that keeps clear of parts, terminals and the wires already there.
 */

#include "editing/wire_routing.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <functional>
#include <queue>
#include <utility>

namespace GUI {

namespace {

/// How far past the ends and the blocked areas a route may wander, in grid units.
constexpr int ROUTE_MARGIN = 4;
/// The most grid points a search looks at, so a route between far ends of a huge schematic gives up instead of
/// stalling the editor.
constexpr std::size_t MAX_ROUTING_POINTS = 250000;
/// What one bend costs, in grid units of length: a route takes a detour this long to save a bend.
constexpr int BEND_COST = 4;

/// The four ways a wire can go: right, left, down and up.
constexpr std::array<GridPoint, 4> DIRECTIONS = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
/// The direction a search starts with, before its first step.
constexpr std::size_t NO_DIRECTION = DIRECTIONS.size();
/// The directions per point, the starting one included.
constexpr std::size_t STATES_PER_POINT = DIRECTIONS.size() + 1;

bool IsHorizontalDirection(const std::size_t direction) {
    return DIRECTIONS[direction].Y == 0;
}

bool AreOpposite(const std::size_t first, const std::size_t second) {
    return DIRECTIONS[first].X == -DIRECTIONS[second].X && DIRECTIONS[first].Y == -DIRECTIONS[second].Y;
}

/**
 * @struct  Region
 * @brief   The rectangle a search covers, corners included, with each of its points numbered row by row.
 */
struct Region {
    GridPoint Min;
    GridPoint Max;

    int GetWidth() const { return Max.X - Min.X + 1; }
    std::size_t GetSize() const {
        return static_cast<std::size_t>(GetWidth()) * static_cast<std::size_t>(Max.Y - Min.Y + 1);
    }
    bool Contains(const GridPoint point) const {
        return point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y;
    }
    std::size_t GetIndex(const GridPoint point) const {
        return static_cast<std::size_t>(point.Y - Min.Y) * static_cast<std::size_t>(GetWidth()) +
               static_cast<std::size_t>(point.X - Min.X);
    }
    GridPoint GetPoint(const std::size_t index) const {
        const auto width = static_cast<std::size_t>(GetWidth());
        return {Min.X + static_cast<int>(index % width), Min.Y + static_cast<int>(index / width)};
    }
};

void Include(Region &region, const GridPoint point) {
    region.Min = {std::min(region.Min.X, point.X), std::min(region.Min.Y, point.Y)};
    region.Max = {std::max(region.Max.X, point.X), std::max(region.Max.Y, point.Y)};
}

/**
 * @brief   Keeps only the points of a path where it bends, and its two ends.
 */
std::vector<GridPoint> KeepCorners(const std::vector<GridPoint> &path) {
    std::vector<GridPoint> corners;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index == 0 || index + 1 == path.size()) {
            corners.push_back(path[index]);
            continue;
        }
        const GridPoint before = path[index - 1];
        const GridPoint after = path[index + 1];
        if (before.X != after.X && before.Y != after.Y) {
            corners.push_back(path[index]);
        }
    }
    return corners;
}

} // namespace

/**
 * @brief   Starts a grid where every wire and terminal of a schematic is an obstacle.
 * @param[in] wires      The wires already drawn.
 * @param[in] terminals  Every terminal of the parts.
 */
RoutingGrid::RoutingGrid(const std::vector<UIWire> &wires, const std::vector<GridPoint> &terminals) {
    for (const UIWire &wire : wires) {
        AddWire(wire);
    }
    for (const GridPoint terminal : terminals) {
        Mark(terminal, Use::BLOCKED);
    }
}

/**
 * @brief   Keeps routes off a rectangle, such as the body of a part.
 * @param[in] min  Corner with the lowest X and Y.
 * @param[in] max  Corner with the highest X and Y; both corners and the edges between them are blocked too.
 */
void RoutingGrid::BlockArea(const GridPoint min, const GridPoint max) {
    m_Areas.push_back({.Min = min, .Max = max});
    for (int y = min.Y; y <= max.Y; ++y) {
        for (int x = min.X; x <= max.X; ++x) {
            Mark({x, y}, Use::BLOCKED);
        }
    }
}

/**
 * @brief   Makes a wire an obstacle, such as one routed before, so the next route keeps clear of it.
 * @param[in] wire  A horizontal or vertical wire; a slanted one only blocks its ends.
 */
void RoutingGrid::AddWire(const UIWire &wire) {
    const GridPoint start = wire.GetStart();
    const GridPoint end = wire.GetEnd();
    if (start.Y == end.Y) {
        for (int x = std::min(start.X, end.X) + 1; x < std::max(start.X, end.X); ++x) {
            Mark({x, start.Y}, Use::HORIZONTAL);
        }
    } else if (start.X == end.X) {
        for (int y = std::min(start.Y, end.Y) + 1; y < std::max(start.Y, end.Y); ++y) {
            Mark({start.X, y}, Use::VERTICAL);
        }
    }
    Mark(start, Use::BLOCKED);
    Mark(end, Use::BLOCKED);
}

/**
 * @brief   Finds the shortest path from one point to another, counting each bend as a detour of a few units.
 * @param[in] from  Where the wire starts, usually the end of a wire it continues; it may be an obstacle itself.
 * @param[in] to    Where it ends, usually a terminal; it may be an obstacle itself.
 * @return  The ends of the path and the points where it bends, in order; none when no path keeps clear of the
 *          obstacles near the two points and the blocked areas.
 */
std::optional<std::vector<GridPoint>> RoutingGrid::Route(const GridPoint from, const GridPoint to) const {
    Region region{.Min = from, .Max = from};
    Include(region, to);
    for (const Area &area : m_Areas) {
        Include(region, area.Min);
        Include(region, area.Max);
    }
    region.Min = {region.Min.X - ROUTE_MARGIN, region.Min.Y - ROUTE_MARGIN};
    region.Max = {region.Max.X + ROUTE_MARGIN, region.Max.Y + ROUTE_MARGIN};
    if (region.GetSize() > MAX_ROUTING_POINTS) {
        return std::nullopt;
    }

    const std::size_t state_count = region.GetSize() * STATES_PER_POINT;
    std::vector<int> costs(state_count, INT_MAX);
    std::vector<std::size_t> previous(state_count, state_count);
    using Entry = std::pair<int, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    const std::size_t start = region.GetIndex(from) * STATES_PER_POINT + NO_DIRECTION;
    costs[start] = 0;
    queue.emplace(0, start);

    while (!queue.empty()) {
        const auto [cost, state] = queue.top();
        queue.pop();
        if (cost > costs[state]) {
            continue;
        }
        const GridPoint point = region.GetPoint(state / STATES_PER_POINT);
        const std::size_t direction = state % STATES_PER_POINT;
        if (point == to) {
            std::vector<GridPoint> path;
            for (std::size_t step = state; step != state_count; step = previous[step]) {
                path.push_back(region.GetPoint(step / STATES_PER_POINT));
            }
            std::ranges::reverse(path);
            return KeepCorners(path);
        }
        // A route that crosses a wire goes straight on over it, since a bend there would join the two
        const auto here = m_Uses.find(point);
        const bool is_crossing = point != from && here != m_Uses.end();
        for (std::size_t next_direction = 0; next_direction < DIRECTIONS.size(); ++next_direction) {
            if (direction != NO_DIRECTION &&
                (AreOpposite(direction, next_direction) || (is_crossing && next_direction != direction))) {
                continue;
            }
            const GridPoint next = {point.X + DIRECTIONS[next_direction].X, point.Y + DIRECTIONS[next_direction].Y};
            if (!region.Contains(next)) {
                continue;
            }
            if (next != to) {
                const auto there = m_Uses.find(next);
                if (there != m_Uses.end() &&
                    (there->second == Use::BLOCKED ||
                     (there->second == Use::HORIZONTAL) == IsHorizontalDirection(next_direction))) {
                    continue;
                }
            }
            const bool is_bend = direction != NO_DIRECTION && next_direction != direction;
            const int next_cost = cost + 1 + (is_bend ? BEND_COST : 0);
            const std::size_t next_state = region.GetIndex(next) * STATES_PER_POINT + next_direction;
            if (next_cost < costs[next_state]) {
                costs[next_state] = next_cost;
                previous[next_state] = state;
                queue.emplace(next_cost, next_state);
            }
        }
    }
    return std::nullopt;
}

/**
 * @brief   Records what lies on a point; a point used twice in different ways, such as two wires crossing, becomes an
 *          obstacle.
 */
void RoutingGrid::Mark(const GridPoint point, const Use use) {
    const auto [found, is_new] = m_Uses.try_emplace(point, use);
    if (!is_new && found->second != use) {
        found->second = Use::BLOCKED;
    }
}

} // namespace GUI
