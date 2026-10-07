/**
 * @file    wire_editing_tests.cpp
 * @brief   Tests for wires following moved terminals and for the cleanup after a move.
 */

#include "test_printers.h"
#include "wire_editing.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace {

bool HasWire(const std::vector<GUI::UIWire> &wires, const GUI::GridPoint first, const GUI::GridPoint second) {
    return std::ranges::any_of(wires, [first, second](const GUI::UIWire &wire) {
        return (wire.GetStart() == first && wire.GetEnd() == second) ||
               (wire.GetStart() == second && wire.GetEnd() == first);
    });
}

} // namespace

TEST_CASE("A wire stays straight when its terminal moves along it", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{-6, 0}, {-2, 0}}};
    const std::vector<GUI::UIWire> moved = GUI::FollowTerminals(wires, {{-2, 0}}, {{-1, 0}});
    CAPTURE(moved);
    REQUIRE(moved.size() == 1);
    CHECK(HasWire(moved, {-6, 0}, {-1, 0}));
}

TEST_CASE("A wire that would turn diagonal becomes an L", "[wire_editing]") {
    SECTION("the leg reaching a horizontal wire's terminal stays horizontal") {
        const std::vector<GUI::UIWire> wires{{{-6, 0}, {-2, 0}}};
        const std::vector<GUI::UIWire> moved = GUI::FollowTerminals(wires, {{-2, 0}}, {{-2, -2}});
        CAPTURE(moved);
        REQUIRE(moved.size() == 2);
        CHECK(HasWire(moved, {-6, 0}, {-6, -2}));
        CHECK(HasWire(moved, {-6, -2}, {-2, -2}));
    }
    SECTION("the leg reaching a vertical wire's terminal stays vertical") {
        const std::vector<GUI::UIWire> wires{{{2, 0}, {2, 4}}};
        const std::vector<GUI::UIWire> moved = GUI::FollowTerminals(wires, {{2, 0}}, {{3, 0}});
        CAPTURE(moved);
        REQUIRE(moved.size() == 2);
        CHECK(HasWire(moved, {2, 4}, {3, 4}));
        CHECK(HasWire(moved, {3, 4}, {3, 0}));
    }
}

TEST_CASE("Wires not touching a moved terminal are left alone", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{-6, 0}, {-6, 4}}};
    const std::vector<GUI::UIWire> moved = GUI::FollowTerminals(wires, {{-2, 0}}, {{0, -3}});
    CHECK(moved.size() == 1);
    CHECK(HasWire(moved, {-6, 0}, {-6, 4}));
}

TEST_CASE("Moving back to the start restores the original wires", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{-6, 0}, {-2, 0}}, {{2, 0}, {2, 4}}};
    const std::vector<GUI::GridPoint> terminals{{-2, 0}, {2, 0}};
    const std::vector<GUI::UIWire> restored = GUI::FollowTerminals(wires, terminals, terminals);
    REQUIRE(restored.size() == wires.size());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        CHECK(restored[index].GetStart() == wires[index].GetStart());
        CHECK(restored[index].GetEnd() == wires[index].GetEnd());
    }
}

TEST_CASE("A wire that shrinks to a point is dropped", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {2, 0}}};
    CHECK(GUI::FollowTerminals(wires, {{2, 0}}, {{0, 0}}).empty());
}

TEST_CASE("SimplifyWires removes duplicates and zero-length wires", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {3, 0}}, {{3, 0}, {0, 0}}, {{1, 1}, {1, 1}}};
    const std::vector<GUI::UIWire> simplified = GUI::SimplifyWires(wires, {});
    CAPTURE(simplified);
    REQUIRE(simplified.size() == 1);
    CHECK(HasWire(simplified, {0, 0}, {3, 0}));
}

TEST_CASE("SimplifyWires joins straight runs at points nothing else uses", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 5}, {3, 5}}, {{3, 5}, {7, 5}}, {{7, 5}, {9, 5}}};

    SECTION("everything merges into one wire") {
        const std::vector<GUI::UIWire> simplified = GUI::SimplifyWires(wires, {});
        CAPTURE(simplified);
        REQUIRE(simplified.size() == 1);
        CHECK(HasWire(simplified, {0, 5}, {9, 5}));
    }
    SECTION("but never across a terminal, so wires keep ending on it") {
        const std::vector<GUI::UIWire> simplified = GUI::SimplifyWires(wires, {{7, 5}});
        CAPTURE(simplified);
        REQUIRE(simplified.size() == 2);
        CHECK(HasWire(simplified, {0, 5}, {7, 5}));
        CHECK(HasWire(simplified, {7, 5}, {9, 5}));
    }
}

TEST_CASE("SimplifyWires keeps a split where another wire passes through", "[wire_editing]") {
    // Merging would leave (3, 0) in the middle of both wires, which would disconnect them
    const std::vector<GUI::UIWire> wires{{{0, 0}, {3, 0}}, {{3, 0}, {6, 0}}, {{3, -2}, {3, 2}}};
    CHECK(GUI::SimplifyWires(wires, {}).size() == 3);
}

TEST_CASE("SimplifyWires keeps bends and wires that double back", "[wire_editing]") {
    CHECK(GUI::SimplifyWires({{{0, 0}, {3, 0}}, {{3, 0}, {3, 3}}}, {}).size() == 2);
    CHECK(GUI::SimplifyWires({{{0, 0}, {5, 0}}, {{5, 0}, {2, 0}}}, {}).size() == 2);
}
