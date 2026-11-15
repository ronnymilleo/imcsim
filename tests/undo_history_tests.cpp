/**
 * @file    undo_history_tests.cpp
 * @brief   Tests for the undo and redo steps of a document kept as snapshots.
 */

#include "schematic/undo_history.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("A new history has nothing to undo or redo and sits at its saved state", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
    CHECK_FALSE(history.HasPendingChange());
    CHECK(history.IsAtSavedState());
}

TEST_CASE("A pending change can be undone before it is committed", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.NoteChange();
    CHECK(history.HasPendingChange());
    CHECK(history.CanUndo());
}

TEST_CASE("Committing a snapshot adds one step that undoes and redoes", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.NoteChange();
    history.Commit("second");
    CHECK_FALSE(history.HasPendingChange());
    CHECK(history.CanUndo());
    CHECK(history.Undo() == "first");
    CHECK_FALSE(history.CanUndo());
    CHECK(history.CanRedo());
    CHECK(history.Redo() == "second");
    CHECK_FALSE(history.CanRedo());
}

TEST_CASE("Committing the snapshot that is already current adds no step", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.NoteChange();
    history.Commit("first");
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.HasPendingChange());
}

TEST_CASE("Undo and redo give nothing when there is no step", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    CHECK_FALSE(history.Undo().has_value());
    CHECK_FALSE(history.Redo().has_value());
}

TEST_CASE("A new step clears what could be redone", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.Commit("second");
    static_cast<void>(history.Undo());
    CHECK(history.CanRedo());
    history.Commit("third");
    CHECK_FALSE(history.CanRedo());
    CHECK(history.Undo() == "first");
}

TEST_CASE("Redoing drops a pending change, which the restored state replaces", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.Commit("second");
    static_cast<void>(history.Undo());
    history.NoteChange();
    CHECK(history.Redo() == "second");
    CHECK_FALSE(history.HasPendingChange());
}

TEST_CASE("Older steps are dropped beyond the limit", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("0");
    for (std::size_t step = 1; step <= GUI::UndoHistory::MAX_UNDO_STEPS + 10; ++step) {
        history.Commit(std::to_string(step));
    }
    std::size_t undone = 0;
    while (history.Undo()) {
        ++undone;
    }
    CHECK(undone == GUI::UndoHistory::MAX_UNDO_STEPS);
}

TEST_CASE("Going back to the saved state is recognised, and moving away from it too", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.Commit("second");
    CHECK_FALSE(history.IsAtSavedState());
    history.MarkSaved();
    CHECK(history.IsAtSavedState());
    history.Commit("third");
    CHECK_FALSE(history.IsAtSavedState());
    static_cast<void>(history.Undo());
    CHECK(history.IsAtSavedState());
}

TEST_CASE("Resetting makes the snapshot the only and the saved state", "[undo_history]") {
    GUI::UndoHistory history;
    history.Reset("first");
    history.Commit("second");
    history.NoteChange();
    history.Reset("other");
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
    CHECK_FALSE(history.HasPendingChange());
    CHECK(history.IsAtSavedState());
}
