/**
 * @file    ui_wire_tests.cpp
 * @brief   Tests for wire interior checks and picking.
 */

#include "ui_elements/ui_wire.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("PassesThrough is true only strictly between the ends", "[ui_wire]") {
    const GUI::UIWire horizontal({0, 0}, {4, 0});
    CHECK(horizontal.PassesThrough({2, 0}));
    CHECK_FALSE(horizontal.PassesThrough({0, 0}));
    CHECK_FALSE(horizontal.PassesThrough({4, 0}));
    CHECK_FALSE(horizontal.PassesThrough({5, 0}));
    CHECK_FALSE(horizontal.PassesThrough({2, 1}));

    SECTION("the direction of the wire does not matter") {
        const GUI::UIWire reversed({4, 0}, {0, 0});
        CHECK(reversed.PassesThrough({1, 0}));
    }
    SECTION("vertical wires work the same way") {
        const GUI::UIWire vertical({3, -2}, {3, 2});
        CHECK(vertical.PassesThrough({3, 0}));
        CHECK_FALSE(vertical.PassesThrough({3, 2}));
        CHECK_FALSE(vertical.PassesThrough({2, 0}));
    }
}

TEST_CASE("IsNear accepts positions within the tolerance of the segment", "[ui_wire]") {
    const GUI::UIWire wire({0, 0}, {4, 0});
    CHECK(wire.IsNear({2.0f, 0.2f}, 0.25f));
    CHECK(wire.IsNear({4.2f, 0.0f}, 0.25f));
    CHECK_FALSE(wire.IsNear({2.0f, 0.3f}, 0.25f));
    CHECK_FALSE(wire.IsNear({4.3f, 0.0f}, 0.25f));
}
