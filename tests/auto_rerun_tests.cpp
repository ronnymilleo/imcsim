/**
 * @file    auto_rerun_tests.cpp
 * @brief   Tests for running the selected analysis again by itself after an edit of the circuit.
 */

#include "auto_rerun.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

namespace {

constexpr double FRAME_SECONDS = 1.0 / 60.0;
constexpr double SETTLE_SECONDS = 0.5;

/**
 * @struct  Session
 * @brief   Frames of the program as a test sees them: a clock, the versions and the state of the selected analysis.
 */
struct Session {
    GUI::AutoRerun Rerun;
    GUI::AutoRerunFrame Frame{.IsEnabled = true, .SettleSeconds = SETTLE_SECONDS, .HasRunToFollow = true};
    std::size_t Runs = 0;

    // Advances the clock by one frame, counting the runs it asks for
    bool Step() {
        Frame.Time += FRAME_SECONDS;
        const bool should_run = Rerun.Update(Frame);
        if (should_run) {
            ++Runs;
        }
        return should_run;
    }

    void Wait(const double seconds) {
        const double end = Frame.Time + seconds;
        while (Frame.Time < end) {
            Step();
        }
    }

    // An edit drops the result, so the analysis has nothing left to follow until it runs again
    void Edit() {
        ++Frame.CircuitVersion;
        Frame.HasRunToFollow = false;
        Step();
    }
};

} // namespace

TEST_CASE("An edit of an analysis that was run runs it again once the edit settles", "[auto_rerun]") {
    Session session;
    session.Step();
    session.Edit();
    CHECK(session.Rerun.IsDue());
    session.Wait(SETTLE_SECONDS / 2.0);
    CHECK(session.Runs == 0);
    session.Wait(SETTLE_SECONDS);
    CHECK(session.Runs == 1);
    CHECK_FALSE(session.Rerun.IsDue());
    // Nothing more without another edit
    session.Wait(2.0);
    CHECK(session.Runs == 1);
}

TEST_CASE("A longer settle time waits longer after the edit", "[auto_rerun]") {
    Session session;
    session.Frame.SettleSeconds = 3.0;
    session.Step();
    session.Edit();
    session.Wait(2.0);
    CHECK(session.Runs == 0);
    session.Wait(1.5);
    CHECK(session.Runs == 1);
}

TEST_CASE("Nothing runs by itself for an analysis that was never run", "[auto_rerun]") {
    Session session;
    session.Frame.HasRunToFollow = false;
    session.Step();
    session.Edit();
    session.Wait(2.0);
    CHECK(session.Runs == 0);
}

TEST_CASE("A burst of edits runs once, after the last of them", "[auto_rerun]") {
    Session session;
    session.Step();
    for (int edit = 0; edit < 5; ++edit) {
        session.Edit();
        session.Wait(SETTLE_SECONDS / 2.0);
    }
    CHECK(session.Runs == 0);
    session.Wait(SETTLE_SECONDS);
    CHECK(session.Runs == 1);
}

TEST_CASE("A field being typed in, a drag or a run in progress holds the run until it ends", "[auto_rerun]") {
    Session session;
    session.Step();
    session.Edit();
    session.Frame.IsEditing = true;
    session.Wait(2.0);
    CHECK(session.Runs == 0);
    session.Frame.IsEditing = false;
    // The run the edit cancelled is still ending
    session.Frame.IsRunBlocked = true;
    session.Wait(2.0);
    CHECK(session.Runs == 0);
    session.Frame.IsRunBlocked = false;
    CHECK(session.Step());
}

TEST_CASE("An edit while a run is in progress is followed once the run ends", "[auto_rerun]") {
    Session session;
    session.Frame.HasRunToFollow = false;
    session.Step();
    // The user starts the first run; the analysis is followed from then on, before it has a result
    session.Frame.HasRunToFollow = true;
    session.Frame.IsRunBlocked = true;
    session.Step();
    session.Edit();
    session.Wait(1.0);
    session.Frame.IsRunBlocked = false;
    session.Wait(1.0);
    CHECK(session.Runs == 1);
}

TEST_CASE("With the preference off nothing runs, and turning it on later does not run old edits", "[auto_rerun]") {
    Session session;
    session.Frame.IsEnabled = false;
    session.Step();
    session.Edit();
    session.Wait(2.0);
    CHECK(session.Runs == 0);
    session.Frame.IsEnabled = true;
    session.Wait(2.0);
    CHECK(session.Runs == 0);
}

TEST_CASE("Opening another schematic forgets the analysis that was followed", "[auto_rerun]") {
    Session session;
    session.Step();
    // Opening a file replaces the content, which also drops the results
    ++session.Frame.DocumentVersion;
    ++session.Frame.CircuitVersion;
    session.Frame.HasRunToFollow = false;
    session.Step();
    session.Edit();
    session.Wait(2.0);
    CHECK(session.Runs == 0);
}
