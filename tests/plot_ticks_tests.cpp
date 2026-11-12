/**
 * @file    plot_ticks_tests.cpp
 * @brief   Tests for the tick positions and labels of plot axes.
 */

#include "plots/plot_ticks.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

namespace {

using Catch::Matchers::WithinAbs;

} // namespace

TEST_CASE("Nice ticks are round numbers across the range", "[plot_ticks]") {
    const std::vector<double> ticks = GUI::FindNiceTicks(0.0, 1.0, 5);
    REQUIRE(ticks.size() == 6);
    CHECK_THAT(ticks[1], WithinAbs(0.2, 1e-12));
    CHECK_THAT(ticks.back(), WithinAbs(1.0, 1e-12));

    const std::vector<double> around_zero = GUI::FindNiceTicks(-0.3, 0.3, 6);
    CHECK(std::ranges::find(around_zero, 0.0) != around_zero.end());
    CHECK(GUI::FindNiceTicks(1.0, 1.0, 5).empty());
}

TEST_CASE("Decade ticks skip decades when there are too many", "[plot_ticks]") {
    CHECK(GUI::FindDecadeTicks(1.0, 1e6, 10).size() == 7);
    const std::vector<double> thinned = GUI::FindDecadeTicks(1.0, 1e6, 3);
    CHECK(thinned.size() <= 3);
    CHECK_THAT(thinned.front(), WithinAbs(1.0, 1e-12));
    CHECK(GUI::FindDecadeTicks(-1.0, 10.0, 5).empty());
}

TEST_CASE("Tick labels show the step with the fewest decimals", "[plot_ticks]") {
    CHECK(GUI::FormatTick(0.5, 0.1) == "0.5");
    CHECK(GUI::FormatTick(20.0, 10.0) == "20");
    CHECK(GUI::FormatTick(-1e-17, 0.2) == "0");
    CHECK(GUI::FormatTick(-0.0, 1.0) == "0");
    CHECK(GUI::FormatTick(2e-5, 1e-5) == "2e-5");
}
