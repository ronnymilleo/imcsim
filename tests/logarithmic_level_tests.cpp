/**
 * @file    logarithmic_level_tests.cpp
 * @brief   Tests for the logarithmic scale the heat colors and the sensitivity views place values on.
 */

#include "results/logarithmic_level.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

using Catch::Matchers::WithinAbs;

constexpr double DECADES = 4.0;

} // namespace

TEST_CASE("The largest value sits at the top of the scale and each decade below it is an equal step",
          "[logarithmic_level]") {
    CHECK_THAT(GUI::LogarithmicLevel(100.0, 100.0, DECADES), WithinAbs(1.0, 1e-6));
    CHECK_THAT(GUI::LogarithmicLevel(10.0, 100.0, DECADES), WithinAbs(0.75, 1e-6));
    CHECK_THAT(GUI::LogarithmicLevel(1.0, 100.0, DECADES), WithinAbs(0.5, 1e-6));
    CHECK_THAT(GUI::LogarithmicLevel(0.01, 100.0, DECADES), WithinAbs(0.0, 1e-6));
}

TEST_CASE("Only the size of a value counts, not its sign", "[logarithmic_level]") {
    CHECK(GUI::LogarithmicLevel(-1.0, 100.0, DECADES) == GUI::LogarithmicLevel(1.0, 100.0, DECADES));
}

TEST_CASE("A value of zero or a scale without a largest value reads zero", "[logarithmic_level]") {
    CHECK(GUI::LogarithmicLevel(0.0, 100.0, DECADES) == 0.0f);
    CHECK(GUI::LogarithmicLevel(5.0, 0.0, DECADES) == 0.0f);
    CHECK(GUI::LogarithmicLevel(5.0, -1.0, DECADES) == 0.0f);
}

TEST_CASE("A value beyond the scale is not clamped, for the caller to decide", "[logarithmic_level]") {
    CHECK(GUI::LogarithmicLevel(1000.0, 100.0, DECADES) > 1.0f);
    CHECK(GUI::LogarithmicLevel(1e-6, 100.0, DECADES) < 0.0f);
}
