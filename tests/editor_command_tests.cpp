/**
 * @file    editor_command_tests.cpp
 * @brief   Tests for the commands of the editor: what the turning ones ask of the selection, and which wait for a drag.
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

TEST_CASE("Each turn of the selection has the command that asks for it", "[editor_command]") {
    using Kind = GUI::PointTransform::Kind;
    for (const GUI::EditorCommand command : {GUI::EditorCommand::ROTATE,
                                             GUI::EditorCommand::MIRROR,
                                             GUI::EditorCommand::FLIP,
                                             GUI::EditorCommand::ROTATE_KEEP_WIRES,
                                             GUI::EditorCommand::MIRROR_KEEP_WIRES,
                                             GUI::EditorCommand::FLIP_KEEP_WIRES}) {
        const auto turn = GUI::GetTurnRequest(command);
        REQUIRE(turn);
        CHECK(GUI::GetTurnCommand(*turn) == command);
    }
    // Alt+Shift+M, as the selecting mode reads it
    CHECK(GUI::GetTurnCommand({.Kind = Kind::MIRROR_TOP_BOTTOM, .IsKeepingWires = true})
          == GUI::EditorCommand::FLIP_KEEP_WIRES);
    // No command moves the selection without turning it
    CHECK_FALSE(GUI::GetTurnCommand({.Kind = Kind::TRANSLATE, .IsKeepingWires = false}));
}

TEST_CASE("Only the commands that remove or replace elements wait for a drag to end", "[editor_command]") {
    CHECK_FALSE(GUI::CanRunDuringDrag(GUI::EditorCommand::UNDO));
    CHECK_FALSE(GUI::CanRunDuringDrag(GUI::EditorCommand::REDO));
    CHECK_FALSE(GUI::CanRunDuringDrag(GUI::EditorCommand::CUT));
    CHECK_FALSE(GUI::CanRunDuringDrag(GUI::EditorCommand::DELETE_SELECTION));
    CHECK(GUI::CanRunDuringDrag(GUI::EditorCommand::COPY));
    CHECK(GUI::CanRunDuringDrag(GUI::EditorCommand::SELECT_ALL));
    CHECK(GUI::CanRunDuringDrag(GUI::EditorCommand::ROTATE));
    CHECK(GUI::CanRunDuringDrag(GUI::EditorCommand::FIT));
}
