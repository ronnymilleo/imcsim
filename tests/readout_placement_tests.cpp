/**
 * @file    readout_placement_tests.cpp
 * @brief   Tests for where the voltage readout of a node sits beside its wire.
 */

#include "drawing/readout_placement.h"

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr ImVec2 READOUT_SIZE = {80.0f, 20.0f};
constexpr float GAP = 5.0f;

} // namespace

TEST_CASE("A readout sits under a level wire, centered on it", "[readout_placement]") {
    const ImVec2 corner = GUI::PlaceReadout({100.0f, 50.0f}, {300.0f, 50.0f}, READOUT_SIZE, GAP);
    CHECK(corner.x == 160.0f);
    // Under the wire, so it keeps off the probe name over it
    CHECK(corner.y == 55.0f);
}

TEST_CASE("A readout sits right of an upright wire, centered on it", "[readout_placement]") {
    const ImVec2 corner = GUI::PlaceReadout({100.0f, 300.0f}, {100.0f, 100.0f}, READOUT_SIZE, GAP);
    CHECK(corner.x == 105.0f);
    CHECK(corner.y == 190.0f);
}

TEST_CASE("A readout at a terminal sits under and right of it", "[readout_placement]") {
    const ImVec2 corner = GUI::PlaceReadout({100.0f, 100.0f}, {100.0f, 100.0f}, READOUT_SIZE, GAP);
    CHECK(corner.x == 105.0f);
    CHECK(corner.y == 105.0f);
}
