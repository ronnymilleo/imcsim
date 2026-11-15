/**
 * @file    wire_routing_tests.cpp
 * @brief   Tests for routing new wires on the grid around parts, terminals and other wires.
 */

#include "schematic/wire_routing.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

namespace {

/**
 * @brief   Every grid point a path goes over, its ends and corners included.
 */
std::vector<GUI::GridPoint> ListPoints(const std::vector<GUI::GridPoint> &corners) {
    std::vector<GUI::GridPoint> points;
    for (std::size_t index = 1; index < corners.size(); ++index) {
        GUI::GridPoint point = corners[index - 1];
        const GUI::GridPoint end = corners[index];
        const GUI::GridPoint step = {(end.X > point.X) - (end.X < point.X), (end.Y > point.Y) - (end.Y < point.Y)};
        while (point != end) {
            points.push_back(point);
            point = {point.X + step.X, point.Y + step.Y};
        }
    }
    points.push_back(corners.back());
    return points;
}

} // namespace

TEST_CASE("A route with nothing in its way is a straight wire, or one bend", "[wire_routing]") {
    const GUI::RoutingGrid grid({}, {});
    CHECK(grid.Route({0, 0}, {5, 0}) == std::vector<GUI::GridPoint>{{0, 0}, {5, 0}});
    const auto bent = grid.Route({0, 0}, {5, 3});
    REQUIRE(bent);
    CHECK(bent->size() == 3);
    CHECK(bent->front() == GUI::GridPoint{0, 0});
    CHECK(bent->back() == GUI::GridPoint{5, 3});
}

TEST_CASE("A route goes around a blocked area, edges included", "[wire_routing]") {
    GUI::RoutingGrid grid({}, {});
    grid.BlockArea({2, -2}, {4, 2});
    const auto route = grid.Route({0, 0}, {6, 0});
    REQUIRE(route);
    for (const GUI::GridPoint point : ListPoints(*route)) {
        const bool is_blocked = point.X >= 2 && point.X <= 4 && point.Y >= -2 && point.Y <= 2;
        CHECK_FALSE(is_blocked);
    }
    // Over or under the area, with the fewest bends: out, along and back
    CHECK(route->size() == 4);
}

TEST_CASE("A route may end on a blocked point, such as a terminal on the edge of a box", "[wire_routing]") {
    GUI::RoutingGrid grid({}, {{0, -3}, {3, -3}});
    // The box takes the row of its leads too, so the terminal on its top edge can only be reached from above
    grid.BlockArea({-2, -3}, {2, 2});
    const auto route = grid.Route({-6, 0}, {0, -3});
    REQUIRE(route);
    CHECK(route->back() == GUI::GridPoint{0, -3});
    const std::vector<GUI::GridPoint> points = ListPoints(*route);
    CHECK(points[points.size() - 2] == GUI::GridPoint{0, -4});
    for (const GUI::GridPoint point : points) {
        CHECK(point != GUI::GridPoint{3, -3});
    }
}

TEST_CASE("A route crosses a wire at a right angle but never bends on it or runs along it", "[wire_routing]") {
    // A long vertical wire in the way, which the route has to cross
    const GUI::RoutingGrid grid({GUI::UIWire({3, -20}, {3, 20})}, {});
    CHECK(grid.Route({0, 0}, {6, 0}) == std::vector<GUI::GridPoint>{{0, 0}, {6, 0}});

    // Reaching a point straight above on the wire's line would run along it, so the route keeps off that line
    const auto route = grid.Route({0, 0}, {6, 5});
    REQUIRE(route);
    for (const GUI::GridPoint corner : *route) {
        CHECK(corner.X != 3);
    }
}

TEST_CASE("A route keeps off terminals and wire ends, which it would connect to", "[wire_routing]") {
    const GUI::RoutingGrid grid({GUI::UIWire({3, 0}, {3, 4})}, {{5, 0}});
    const auto route = grid.Route({0, 0}, {8, 0});
    REQUIRE(route);
    for (const GUI::GridPoint point : ListPoints(*route)) {
        CHECK(point != GUI::GridPoint{3, 0});
        CHECK(point != GUI::GridPoint{5, 0});
    }
}

TEST_CASE("A route gives up when its end is walled in", "[wire_routing]") {
    GUI::RoutingGrid grid({}, {});
    grid.BlockArea({-1, -1}, {1, 1});
    CHECK_FALSE(grid.Route({-6, 0}, {0, 0}));
}

TEST_CASE("A wire added to the grid keeps the next route clear of it", "[wire_routing]") {
    GUI::RoutingGrid grid({}, {});
    const auto first = grid.Route({0, 0}, {6, 0});
    REQUIRE(first);
    grid.AddWire(GUI::UIWire((*first)[0], (*first)[1]));
    // The second route would lie on the first one; it has to find another way
    const auto second = grid.Route({0, 2}, {6, 0});
    REQUIRE(second);
    for (const GUI::GridPoint point : ListPoints(*second)) {
        if (point != GUI::GridPoint{6, 0}) {
            CHECK_FALSE((point.Y == 0 && point.X >= 0 && point.X <= 6));
        }
    }
}
