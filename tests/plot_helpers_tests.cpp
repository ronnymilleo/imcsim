/**
 * @file    plot_helpers_tests.cpp
 * @brief   Tests for the plot geometry: nearest samples and dashed lines.
 */

#include "plot_helpers.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <vector>

namespace {

void CheckPoint(const ImVec2 actual, const ImVec2 expected) {
    CHECK_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, 1e-4));
    CHECK_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, 1e-4));
}

/**
 * @struct  ClipArea
 * @brief   Corners of the visible area handed to SplitIntoDashes.
 */
struct ClipArea {
    ImVec2 Min;
    ImVec2 Max;
};

// Large enough that the tests of the dash pattern clip nothing
constexpr ClipArea WideArea = {{-1000.0f, -1000.0f}, {1000.0f, 1000.0f}};

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
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(line, 3.0f, 2.0f, WideArea.Min, WideArea.Max);
    REQUIRE(dashes.size() == 2);
    CheckPoint(dashes[0].Start, {0.0f, 0.0f});
    CheckPoint(dashes[0].End, {3.0f, 0.0f});
    CheckPoint(dashes[1].Start, {5.0f, 0.0f});
    CheckPoint(dashes[1].End, {8.0f, 0.0f});
}

TEST_CASE("SplitIntoDashes keeps the pattern going around bends", "[plot_helpers]") {
    // 2 pixels right, then 4 down: the first dash turns the corner, a 1 pixel gap follows, then the last dash
    const std::array<ImVec2, 3> polyline = {ImVec2(0.0f, 0.0f), ImVec2(2.0f, 0.0f), ImVec2(2.0f, 4.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(polyline, 3.0f, 1.0f, WideArea.Min, WideArea.Max);
    REQUIRE(dashes.size() == 3);
    CheckPoint(dashes[0].End, {2.0f, 0.0f});
    CheckPoint(dashes[1].Start, {2.0f, 0.0f});
    CheckPoint(dashes[1].End, {2.0f, 1.0f});
    CheckPoint(dashes[2].Start, {2.0f, 2.0f});
    CheckPoint(dashes[2].End, {2.0f, 4.0f});
}

TEST_CASE("SplitIntoDashes skips repeated points", "[plot_helpers]") {
    const std::array<ImVec2, 3> polyline = {ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(polyline, 3.0f, 1.0f, WideArea.Min, WideArea.Max);
    REQUIRE(dashes.size() == 1);
    CheckPoint(dashes[0].End, {1.0f, 0.0f});
}

TEST_CASE("SplitIntoDashes keeps only the visible part of a segment that runs far off screen", "[plot_helpers]") {
    // A zoomed-in plot can put a sample a billion pixels away; dashing the whole segment would never finish
    const std::array<ImVec2, 2> line = {ImVec2(5.0f, 0.0f), ImVec2(5.0f, 1e9f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(line, 3.0f, 2.0f, {0.0f, 0.0f}, {10.0f, 10.0f});
    REQUIRE(dashes.size() == 2);
    CheckPoint(dashes[0].Start, {5.0f, 0.0f});
    CheckPoint(dashes[1].End, {5.0f, 8.0f});
}

TEST_CASE("SplitIntoDashes keeps the pattern in step across a clipped part", "[plot_helpers]") {
    // The first 6 pixels are clipped: one whole dash and gap, then 1 pixel into the next dash
    const std::array<ImVec2, 2> line = {ImVec2(-6.0f, 5.0f), ImVec2(10.0f, 5.0f)};
    const std::vector<GUI::LineSegment> dashes = GUI::SplitIntoDashes(line, 3.0f, 2.0f, {0.0f, 0.0f}, {10.0f, 10.0f});
    REQUIRE(dashes.size() == 3);
    CheckPoint(dashes[0].Start, {0.0f, 5.0f});
    CheckPoint(dashes[0].End, {2.0f, 5.0f});
    CheckPoint(dashes[1].Start, {4.0f, 5.0f});
}

TEST_CASE("SplitIntoDashes skips segments outside the area or with infinite coordinates", "[plot_helpers]") {
    const float infinity = std::numeric_limits<float>::infinity();
    const std::array<ImVec2, 3> polyline = {ImVec2(20.0f, 0.0f), ImVec2(20.0f, 5.0f), ImVec2(5.0f, infinity)};
    CHECK(GUI::SplitIntoDashes(polyline, 3.0f, 2.0f, {0.0f, 0.0f}, {10.0f, 10.0f}).empty());
}

TEST_CASE("FindWidestSpread finds where a family of curves parts most", "[plot_helpers]") {
    // Curves that grow apart toward the end are labeled at the end, as before
    const std::vector<double> low = {0.0, 1.0, 2.0};
    const std::vector<double> high = {0.0, 2.0, 4.0};
    CHECK(GUI::FindWidestSpread({&low, &high}) == 2);

    // Derivatives peak in the middle and all end near zero, so their labels go to the peak
    const std::vector<double> small_peak = {0.0, 3.0, 0.0};
    const std::vector<double> large_peak = {0.0, 9.0, 0.1};
    CHECK(GUI::FindWidestSpread({&small_peak, &large_peak}) == 1);

    // Ties keep the last sample, and values that are not finite are ignored
    const std::vector<double> flat = {1.0, 1.0, 1.0};
    const std::vector<double> parallel = {2.0, 2.0, 2.0};
    CHECK(GUI::FindWidestSpread({&flat, &parallel}) == 2);
    const std::vector<double> broken = {0.0, std::numeric_limits<double>::quiet_NaN(), 1.0};
    const std::vector<double> zero = {0.0, 50.0, 0.0};
    CHECK(GUI::FindWidestSpread({&broken, &zero}) == 2);
    CHECK(GUI::FindWidestSpread({}) == 0);
}
