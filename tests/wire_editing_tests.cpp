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

namespace {

GUI::PointTransform Translation(const int x, const int y) {
    return {.Type = GUI::PointTransform::Kind::TRANSLATE, .Delta = {x, y}, .Pivot = {}};
}

bool IsSelected(const GUI::MovedWires &moved, const GUI::GridPoint first, const GUI::GridPoint second) {
    for (const std::size_t index : moved.Selected) {
        const GUI::UIWire &wire = moved.Wires[index];
        if ((wire.GetStart() == first && wire.GetEnd() == second) ||
            (wire.GetStart() == second && wire.GetEnd() == first)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Points are moved by a translation, a quarter turn and the two mirrors", "[wire_editing]") {
    CHECK(Translation(2, -1).Apply({3, 3}) == GUI::GridPoint{5, 2});
    const GUI::PointTransform turn = {.Type = GUI::PointTransform::Kind::QUARTER_TURN, .Delta = {}, .Pivot = {4, 4}};
    // A quarter turn goes the way NextRotation() does: a point to the right of the pivot ends up below it
    CHECK(turn.Apply({6, 4}) == GUI::GridPoint{4, 6});
    CHECK(turn.Apply({4, 4}) == GUI::GridPoint{4, 4});
    const GUI::PointTransform left_right = {
        .Type = GUI::PointTransform::Kind::MIRROR_LEFT_RIGHT, .Delta = {}, .Pivot = {5, 0}};
    CHECK(left_right.Apply({2, 3}) == GUI::GridPoint{8, 3});
    const GUI::PointTransform top_bottom = {
        .Type = GUI::PointTransform::Kind::MIRROR_TOP_BOTTOM, .Delta = {}, .Pivot = {0, 5}};
    CHECK(top_bottom.Apply({2, 3}) == GUI::GridPoint{2, 7});
}

TEST_CASE("Sliding a segment stretches the wires that meet it at right angles", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, -4}, {0, 0}}, {{0, 0}, {4, 0}}, {{4, 0}, {4, -4}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {1}, {}, {}, {}, Translation(0, 2));

    REQUIRE(moved.Wires.size() == 3);
    CHECK(HasWire(moved.Wires, {0, -4}, {0, 2}));
    CHECK(HasWire(moved.Wires, {4, -4}, {4, 2}));
    CHECK(HasWire(moved.Wires, {0, 2}, {4, 2}));
    CHECK(moved.Selected.size() == 1);
    CHECK(IsSelected(moved, {0, 2}, {4, 2}));
}

TEST_CASE("Sliding a segment bends the wires that continue it", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {2, 0}}, {{2, 0}, {6, 0}}, {{6, 0}, {8, 0}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {1}, {}, {}, {}, Translation(0, 2));

    // The neighbors jog next to their fixed ends and join the moved segment into one run
    REQUIRE(moved.Wires.size() == 3);
    CHECK(HasWire(moved.Wires, {0, 0}, {0, 2}));
    CHECK(HasWire(moved.Wires, {0, 2}, {8, 2}));
    CHECK(HasWire(moved.Wires, {8, 0}, {8, 2}));
    CHECK(IsSelected(moved, {0, 2}, {8, 2}));
}

TEST_CASE("A wire that ends on the middle of a moved segment goes with it", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {6, 0}}, {{3, 0}, {3, -4}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {0}, {}, {}, {}, Translation(0, 2));

    CHECK(HasWire(moved.Wires, {0, 2}, {6, 2}));
    CHECK(HasWire(moved.Wires, {3, -4}, {3, 2}));
    CHECK(moved.Selected.size() == 1);
}

TEST_CASE("A moved wire that ended on a part that stays keeps it connected", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {4, 0}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {0}, {}, {}, {{0, 0}}, Translation(0, 2));

    REQUIRE(moved.Wires.size() == 2);
    CHECK(HasWire(moved.Wires, {0, 0}, {0, 2}));
    CHECK(HasWire(moved.Wires, {0, 2}, {4, 2}));
    CHECK(IsSelected(moved, {0, 2}, {4, 2}));
    CHECK_FALSE(IsSelected(moved, {0, 0}, {0, 2}));
}

TEST_CASE("A wire from a part that stays keeps its end on that part while a segment slides", "[wire_editing]") {
    // The part at (0, 0) stays; the selected segment starts on its terminal and a wire also ends there
    const std::vector<GUI::UIWire> wires{{{-4, 0}, {0, 0}}, {{0, 0}, {4, 0}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {1}, {}, {}, {{0, 0}}, Translation(0, 2));

    CHECK(HasWire(moved.Wires, {-4, 0}, {0, 0}));
    CHECK(HasWire(moved.Wires, {0, 0}, {0, 2}));
    CHECK(HasWire(moved.Wires, {0, 2}, {4, 2}));
}

TEST_CASE("A part that stays and touches a moved wire in the middle stays connected", "[wire_editing]") {
    // A ground at (4, 0) hangs on a rail that slides down
    const std::vector<GUI::UIWire> wires{{{0, 0}, {8, 0}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {0}, {}, {}, {{4, 0}}, Translation(0, 2));

    CHECK(HasWire(moved.Wires, {0, 2}, {8, 2}));
    CHECK(HasWire(moved.Wires, {4, 0}, {4, 2}));
    CHECK(IsSelected(moved, {0, 2}, {8, 2}));
    CHECK_FALSE(IsSelected(moved, {4, 0}, {4, 2}));
}

TEST_CASE("A moved wire that ended on the middle of another wire stays connected to it", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{0, 0}, {8, 0}}, {{4, 0}, {4, 4}}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {1}, {}, {}, {}, Translation(2, 0));

    CHECK(HasWire(moved.Wires, {0, 0}, {8, 0}));
    CHECK(HasWire(moved.Wires, {4, 0}, {6, 0}));
    CHECK(HasWire(moved.Wires, {6, 0}, {6, 4}));
}

TEST_CASE("Parts and wires move together and the wires around them follow", "[wire_editing]") {
    // A part with terminals at (0, 0) and (4, 0) moves one unit up with the wire between its terminal and a corner
    const std::vector<GUI::UIWire> wires{{{-4, 0}, {0, 0}}, {{4, 0}, {4, 4}}};
    const GUI::MovedWires moved =
        GUI::MoveGroup(wires, {1}, {{0, 0}, {4, 0}}, {{0, -1}, {4, -1}}, {}, Translation(0, -1));

    CHECK(HasWire(moved.Wires, {-4, -1}, {0, -1}));
    CHECK(HasWire(moved.Wires, {4, -1}, {4, 3}));
    CHECK(IsSelected(moved, {4, -1}, {4, 3}));
}

TEST_CASE("A selection turned about a point turns its wires and keeps the others attached", "[wire_editing]") {
    // The selected wire runs right of the pivot; another wire continues it to the right
    const std::vector<GUI::UIWire> wires{{{2, 0}, {6, 0}}, {{6, 0}, {10, 0}}};
    const GUI::PointTransform turn = {.Type = GUI::PointTransform::Kind::QUARTER_TURN, .Delta = {}, .Pivot = {0, 0}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {0}, {}, {}, {}, turn);

    // (2, 0)-(6, 0) becomes (0, 2)-(0, 6), and the other wire follows its end to (0, 6) with a bend at its fixed end
    CHECK(IsSelected(moved, {0, 2}, {0, 6}));
    CHECK(moved.Selected.size() == 1);
    CHECK(HasWire(moved.Wires, {10, 0}, {10, 6}));
    CHECK(HasWire(moved.Wires, {10, 6}, {0, 6}));
}

TEST_CASE("A selection mirrored about a point swaps its sides", "[wire_editing]") {
    const std::vector<GUI::UIWire> wires{{{2, 0}, {4, 0}}};
    const GUI::PointTransform mirror = {
        .Type = GUI::PointTransform::Kind::MIRROR_LEFT_RIGHT, .Delta = {}, .Pivot = {5, 0}};
    const GUI::MovedWires moved = GUI::MoveGroup(wires, {0}, {}, {}, {}, mirror);

    REQUIRE(moved.Wires.size() == 1);
    CHECK(HasWire(moved.Wires, {8, 0}, {6, 0}));
    CHECK(IsSelected(moved, {6, 0}, {8, 0}));
}
