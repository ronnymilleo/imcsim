/**
 * @file    playback_tests.cpp
 * @brief   Tests for the instant of a transient the schematic shows.
 */

#include "results/playback.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Playing plays the whole run in the same time whatever its length", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Playing = true;
    playback.Advance(GUI::PLAYBACK_SECONDS / 2.0, 0.0, 10e-3);
    CHECK_THAT(playback.Time, Catch::Matchers::WithinAbs(5e-3, 1e-12));
    CHECK(playback.Playing);
}

TEST_CASE("A paused playback keeps its instant", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Time = 3e-3;
    playback.Advance(1.0, 0.0, 10e-3);
    CHECK(playback.Time == 3e-3);
}

TEST_CASE("At the end of the run playback loops or stops", "[playback]") {
    GUI::TransientPlayback looping;
    looping.Playing = true;
    looping.Time = 9e-3;
    looping.Advance(GUI::PLAYBACK_SECONDS * 0.2, 0.0, 10e-3);
    CHECK_THAT(looping.Time, Catch::Matchers::WithinAbs(1e-3, 1e-12));
    CHECK(looping.Playing);

    GUI::TransientPlayback once;
    once.Loop = false;
    once.Playing = true;
    once.Time = 9e-3;
    once.Advance(GUI::PLAYBACK_SECONDS * 0.2, 0.0, 10e-3);
    CHECK(once.Time == 10e-3);
    CHECK_FALSE(once.Playing);
}

TEST_CASE("The instant stays within the run and resets to its start", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Time = 50e-3;
    playback.Advance(0.0, 0.0, 10e-3);
    CHECK(playback.Time == 10e-3);
    playback.Playing = true;
    playback.Reset();
    CHECK(playback.Time == 0.0);
    CHECK_FALSE(playback.Playing);
}

TEST_CASE("A window plays from its start to its end in the same time as any other", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Playing = true;
    playback.Time = 2e-3;
    // A window of 4 ms, 2 ms to 6 ms: half the playback time takes the instant halfway
    playback.Advance(GUI::PLAYBACK_SECONDS / 2.0, 2e-3, 6e-3);
    CHECK_THAT(playback.Time, Catch::Matchers::WithinAbs(4e-3, 1e-12));
}

TEST_CASE("A window that moved pulls the instant inside, and playing loops back to its start", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Time = 1e-3;
    playback.Advance(0.0, 2e-3, 6e-3);
    CHECK(playback.Time == 2e-3);
    playback.Time = 50e-3;
    playback.Advance(0.0, 2e-3, 6e-3);
    CHECK(playback.Time == 6e-3);

    playback.Playing = true;
    playback.Time = 5e-3;
    playback.Advance(GUI::PLAYBACK_SECONDS * 0.5, 2e-3, 6e-3);
    CHECK_THAT(playback.Time, Catch::Matchers::WithinAbs(3e-3, 1e-12));
    CHECK(playback.Playing);
}

TEST_CASE("A window without a span stops the playback at its start", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Playing = true;
    playback.Time = 3e-3;
    playback.Advance(1.0, 2e-3, 2e-3);
    CHECK(playback.Time == 2e-3);
    CHECK_FALSE(playback.Playing);
}

TEST_CASE("Playback scales with speed at slower and faster presets, with looping on and off", "[playback]") {
    GUI::TransientPlayback slow;
    slow.Playing = true;
    slow.Speed = 0.25;
    // Half of PLAYBACK_SECONDS at 0.25x covers 1/8 of the 8 ms span (1 ms)
    slow.Advance(GUI::PLAYBACK_SECONDS * 0.5, 0.0, 8e-3);
    CHECK_THAT(slow.Time, Catch::Matchers::WithinAbs(1e-3, 1e-12));
    CHECK(slow.Playing);

    GUI::TransientPlayback fast_looping;
    fast_looping.Playing = true;
    fast_looping.Loop = true;
    fast_looping.Speed = 4.0;
    fast_looping.Time = 2e-3;
    // Half of PLAYBACK_SECONDS at 4x covers 2x the 10 ms span (20 ms), wrapping back to 2 ms
    fast_looping.Advance(GUI::PLAYBACK_SECONDS * 0.5, 0.0, 10e-3);
    CHECK_THAT(fast_looping.Time, Catch::Matchers::WithinAbs(2e-3, 1e-12));
    CHECK(fast_looping.Playing);

    GUI::TransientPlayback fast_once;
    fast_once.Playing = true;
    fast_once.Loop = false;
    fast_once.Speed = 4.0;
    fast_once.Advance(GUI::PLAYBACK_SECONDS * 0.3, 0.0, 10e-3);
    CHECK(fast_once.Time == 10e-3);
    CHECK_FALSE(fast_once.Playing);
}

TEST_CASE("Resetting playback returns to the start while preserving the chosen speed", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Speed = 2.0;
    playback.Time = 4e-3;
    playback.Playing = true;
    playback.Reset();
    CHECK(playback.Time == 0.0);
    CHECK_FALSE(playback.Playing);
    CHECK(playback.Speed == 2.0);
}

TEST_CASE("Playback speed presets include 1x and format cleanly", "[playback]") {
    CHECK(GUI::TransientPlayback{}.Speed == 1.0);
    CHECK(std::ranges::contains(GUI::PLAYBACK_SPEED_PRESETS, 1.0));
    CHECK(GUI::FormatPlaybackSpeed(0.1) == "0.1x");
    CHECK(GUI::FormatPlaybackSpeed(0.25) == "0.25x");
    CHECK(GUI::FormatPlaybackSpeed(1.0) == "1x");
    CHECK(GUI::FormatPlaybackSpeed(10.0) == "10x");
}
