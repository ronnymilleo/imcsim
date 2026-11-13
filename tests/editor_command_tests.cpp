/**
 * @file    editor_command_tests.cpp
 * @brief   Tests for what the turning commands of the Edit menu ask of the selection.
 */

#include "schematic/editor_command.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("The turning commands say how to turn and whether the wires stay", "[editor_command]") {
    using Kind = GUI::PointTransform::Kind;
    const auto rotate = GUI::GetTurnRequest(GUI::EditorCommand::ROTATE);
    REQUIRE(rotate);
    CHECK(rotate->Kind == Kind::QUARTER_TURN);
    CHECK_FALSE(rotate->IsKeepingWires);

    CHECK(GUI::GetTurnRequest(GUI::EditorCommand::ROTATE_KEEP_WIRES)->IsKeepingWires);
    CHECK(GUI::GetTurnRequest(GUI::EditorCommand::MIRROR)->Kind == Kind::MIRROR_LEFT_RIGHT);
    CHECK(GUI::GetTurnRequest(GUI::EditorCommand::MIRROR_KEEP_WIRES)->IsKeepingWires);
    CHECK(GUI::GetTurnRequest(GUI::EditorCommand::FLIP)->Kind == Kind::MIRROR_TOP_BOTTOM);
    CHECK(GUI::GetTurnRequest(GUI::EditorCommand::FLIP_KEEP_WIRES)->IsKeepingWires);
}

TEST_CASE("The other commands do not turn anything", "[editor_command]") {
    CHECK_FALSE(GUI::GetTurnRequest(GUI::EditorCommand::UNDO));
    CHECK_FALSE(GUI::GetTurnRequest(GUI::EditorCommand::CUT));
    CHECK_FALSE(GUI::GetTurnRequest(GUI::EditorCommand::SELECT_ALL));
    CHECK_FALSE(GUI::GetTurnRequest(GUI::EditorCommand::SAVE));
}
