/**
 * @file    readout_placement_tests.cpp
 * @brief   Tests for where the voltage readout of a node sits beside its wire.
 */

#include "overlays/readout_placement.h"

#include "base/helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>

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

TEST_CASE("A readout takes the size of the part labels at any zoom", "[readout_placement]") {
    // At 20 pixels per grid unit the part labels are 14 pixels high
    const std::optional<float> font_size = GUI::GetReadoutFontSize(20.0f);
    REQUIRE(font_size.has_value());
    CHECK(*font_size == 20.0f * GUI::LABEL_SCALE);
    CHECK(*GUI::GetReadoutFontSize(40.0f) == 2.0f * *font_size);
}

TEST_CASE("Readouts are left out when zoomed out too far to read them", "[readout_placement]") {
    const float threshold_zoom = GUI::MIN_READOUT_FONT_SIZE / GUI::LABEL_SCALE;
    CHECK(GUI::GetReadoutFontSize(threshold_zoom).has_value());
    CHECK_FALSE(GUI::GetReadoutFontSize(threshold_zoom * 0.9f).has_value());
}
