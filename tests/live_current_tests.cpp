/**
 * @file    live_current_tests.cpp
 * @brief   Tests for where the arrows of the live current sit along a piece of wire.
 */

#include "overlays/live_current.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

/// Positions are sums of floats, so they are compared within this.
constexpr double TOLERANCE = 1e-5;

void CheckAlongX(const std::vector<ImVec2> &centers, const std::vector<float> &expected) {
    REQUIRE(centers.size() == expected.size());
    for (std::size_t index = 0; index < centers.size(); ++index) {
        CHECK_THAT(centers[index].x, WithinAbs(expected[index], TOLERANCE));
        CHECK(centers[index].y == 0.0f);
    }
}

} // namespace

TEST_CASE("Arrows sit a spacing apart along a piece, each whole on it", "[live_current]") {
    // On a piece 3 units long the first arrow fits at 0.6 and the last at 2.4
    const GUI::WireCurrent piece = {.Start = {0, 0}, .End = {3, 0}, .Current = 1e-3};
    CheckAlongX(GUI::ListArrowCenters(piece, {1.0f, 0.0f}, 0.0f), {0.6f, 1.2f, 1.8f, 2.4f});
}

TEST_CASE("Arrows scroll with the phase, and stay on the grid whichever way the piece is drawn", "[live_current]") {
    const GUI::WireCurrent forward = {.Start = {0, 0}, .End = {3, 0}, .Current = 1e-3};
    CheckAlongX(GUI::ListArrowCenters(forward, {1.0f, 0.0f}, 0.3f), {0.3f, 0.9f, 1.5f, 2.1f, 2.7f});
    // The same wire drawn from its other end, with its current flowing toward -X, has its arrows at the same places,
    // listed from the back of the flow
    const GUI::WireCurrent backward = {.Start = {3, 0}, .End = {0, 0}, .Current = 1e-3};
    CheckAlongX(GUI::ListArrowCenters(backward, {-1.0f, 0.0f}, 0.0f), {2.4f, 1.8f, 1.2f, 0.6f});
}
