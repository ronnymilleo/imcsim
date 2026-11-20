/**
 * @file    turn_keys_tests.cpp
 * @brief   Tests for the keys that turn what the editor holds.
 */

#include "windows/turn_keys.h"
#include <catch2/catch_test_macros.hpp>

using Kind = GUI::PointTransform::Kind;

TEST_CASE("R turns a quarter, whatever else is held", "[turn_keys]") {
    CHECK(GUI::GetTurnKind({.IsRotatePressed = true, .IsMirrorPressed = false, .IsShiftHeld = false})
          == Kind::QUARTER_TURN);
    CHECK(GUI::GetTurnKind({.IsRotatePressed = true, .IsMirrorPressed = false, .IsShiftHeld = true})
          == Kind::QUARTER_TURN);
}

TEST_CASE("M mirrors left to right, and top to bottom with Shift", "[turn_keys]") {
    CHECK(GUI::GetTurnKind({.IsRotatePressed = false, .IsMirrorPressed = true, .IsShiftHeld = false})
          == Kind::MIRROR_LEFT_RIGHT);
    CHECK(GUI::GetTurnKind({.IsRotatePressed = false, .IsMirrorPressed = true, .IsShiftHeld = true})
          == Kind::MIRROR_TOP_BOTTOM);
}

TEST_CASE("No turning key asks for nothing, and R wins over M in the same frame", "[turn_keys]") {
    CHECK_FALSE(GUI::GetTurnKind({.IsRotatePressed = false, .IsMirrorPressed = false, .IsShiftHeld = true}));
    CHECK(GUI::GetTurnKind({.IsRotatePressed = true, .IsMirrorPressed = true, .IsShiftHeld = false})
          == Kind::QUARTER_TURN);
}
