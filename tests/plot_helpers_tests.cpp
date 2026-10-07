/**
 * @file    plot_helpers_tests.cpp
 * @brief   Tests for the plot geometry: nearest samples and dashed lines.
 */

#include "plot_helpers.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

namespace {

void CheckPoint(const ImVec2 actual, const ImVec2 expected) {
    CHECK_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, 1e-4));
    CHECK_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, 1e-4));
}

} // namespace

TEST_CASE("FindNearestSample picks the closest X in rising and falling sweeps", "[plot_helpers]") {
    const std::vector<double> rising = {0.0, 1.0, 2.0, 3.0};
    CHECK(GUI::FindNearestSample(rising, 1.4, false) == 1);
    CHECK(GUI::FindNearestSample(rising, 1.6, false) == 2);
    CHECK(GUI::FindNearestSample(rising, -5.0, false) == 0);
    CHECK(GUI::FindNearestSample(rising, 9.0, false) == 3);

    const std::vector<double> falling = {5.0, 4.0, 3.0};
    CHECK(GUI::FindNearestSample(falling, 3.9, false) == 1);
    CHECK(GUI::FindNearestSample({}, 1.0, false) == 0);
}

TEST_CASE("FindNearestSample compares decades on a logarithmic axis", "[plot_helpers]") {
    const std::vector<double> frequencies = {10.0, 100.0, 1000.0};
    // In a straight line 400 Hz is closer to 100 Hz, but in decades it is closer to 1 kHz
    CHECK(GUI::FindNearestSample(frequencies, 400.0, false) == 1);
    CHECK(GUI::FindNearestSample(frequencies, 300.0, true) == 1);
    CHECK(GUI::FindNearestSample(frequencies, 400.0, true) == 2);
}

TEST_CASE("SplitIntoDashes alternates dashes and gaps along a straight line", "[plot_helpers]") {
    const std::array<ImVec2, 2> line = {ImVec2(0.0f, 0.0f), ImVec2(10.0f, 0.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(line, 3.0f, 2.0f);
    REQUIRE(dashes.size() == 2);
    CheckPoint(dashes[0].Start, {0.0f, 0.0f});
    CheckPoint(dashes[0].End, {3.0f, 0.0f});
    CheckPoint(dashes[1].Start, {5.0f, 0.0f});
    CheckPoint(dashes[1].End, {8.0f, 0.0f});
}

TEST_CASE("SplitIntoDashes keeps the pattern going around bends", "[plot_helpers]") {
    // 2 pixels right, then 4 down: the first dash turns the corner, a 1 pixel gap follows, then the last dash
    const std::array<ImVec2, 3> polyline = {ImVec2(0.0f, 0.0f), ImVec2(2.0f, 0.0f), ImVec2(2.0f, 4.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(polyline, 3.0f, 1.0f);
    REQUIRE(dashes.size() == 3);
    CheckPoint(dashes[0].End, {2.0f, 0.0f});
    CheckPoint(dashes[1].Start, {2.0f, 0.0f});
    CheckPoint(dashes[1].End, {2.0f, 1.0f});
    CheckPoint(dashes[2].Start, {2.0f, 2.0f});
    CheckPoint(dashes[2].End, {2.0f, 4.0f});
}

TEST_CASE("SplitIntoDashes skips repeated points", "[plot_helpers]") {
    const std::array<ImVec2, 3> polyline = {ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(polyline, 3.0f, 1.0f);
    REQUIRE(dashes.size() == 1);
    CheckPoint(dashes[0].End, {1.0f, 0.0f});
}
