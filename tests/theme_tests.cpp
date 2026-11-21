/**
 * @file    theme_tests.cpp
 * @brief   Tests for the alpha helpers of the theme colors.
 */

#include "base/theme.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Replacing the alpha of a color keeps its red, green and blue", "[theme]") {
    CHECK(GUI::ReplaceAlpha(IM_COL32(10, 20, 30, 255), 40) == IM_COL32(10, 20, 30, 40));
    CHECK(GUI::ReplaceAlpha(IM_COL32(10, 20, 30, 0), 255) == IM_COL32(10, 20, 30, 255));
}

TEST_CASE("Scaling the alpha of a color multiplies it and keeps its red, green and blue", "[theme]") {
    CHECK(GUI::ScaleAlpha(IM_COL32(10, 20, 30, 200), 0.5f) == IM_COL32(10, 20, 30, 100));
    CHECK(GUI::ScaleAlpha(IM_COL32(10, 20, 30, 200), 1.0f) == IM_COL32(10, 20, 30, 200));
    CHECK(GUI::ScaleAlpha(IM_COL32(10, 20, 30, 200), 0.0f) == IM_COL32(10, 20, 30, 0));
}
