/**
 * @file    playback_tests.cpp
 * @brief   Tests for the instant of a transient the schematic shows.
 */

#include "playback.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Playing plays the whole run in the same time whatever its length", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Playing = true;
    playback.Advance(GUI::PLAYBACK_SECONDS / 2.0, 10e-3);
    CHECK_THAT(playback.Time, Catch::Matchers::WithinAbs(5e-3, 1e-12));
    CHECK(playback.Playing);
}

TEST_CASE("A paused playback keeps its instant", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Time = 3e-3;
    playback.Advance(1.0, 10e-3);
    CHECK(playback.Time == 3e-3);
}

TEST_CASE("At the end of the run playback loops or stops", "[playback]") {
    GUI::TransientPlayback looping;
    looping.Playing = true;
    looping.Time = 9e-3;
    looping.Advance(GUI::PLAYBACK_SECONDS * 0.2, 10e-3);
    CHECK_THAT(looping.Time, Catch::Matchers::WithinAbs(1e-3, 1e-12));
    CHECK(looping.Playing);

    GUI::TransientPlayback once;
    once.Loop = false;
    once.Playing = true;
    once.Time = 9e-3;
    once.Advance(GUI::PLAYBACK_SECONDS * 0.2, 10e-3);
    CHECK(once.Time == 10e-3);
    CHECK_FALSE(once.Playing);
}

TEST_CASE("The instant stays within the run and resets to its start", "[playback]") {
    GUI::TransientPlayback playback;
    playback.Time = 50e-3;
    playback.Advance(0.0, 10e-3);
    CHECK(playback.Time == 10e-3);
    playback.Playing = true;
    playback.Reset();
    CHECK(playback.Time == 0.0);
    CHECK_FALSE(playback.Playing);
}
