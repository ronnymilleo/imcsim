/**
 * @file    math_channel_definition_tests.cpp
 * @brief   Tests for what a math channel computes, as the schematic keeps and saves it.
 */

#include "schematic/math_channel_definition.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("A new channel is numbered after the highest of those there are", "[math_channel_definition]") {
    CHECK(GUI::GetNextChannelName({}) == "M1");
    // A gap left by a removed channel is not filled, and names that are not M and a number are ignored
    const std::vector<GUI::MathChannel> channels = {{.Name = "M1"}, {.Name = "M4"}, {.Name = "Mx"}, {.Name = "Power"}};
    CHECK(GUI::GetNextChannelName(channels) == "M5");
}

TEST_CASE("Operations read back from the symbols they are written with", "[math_channel_definition]") {
    for (const GUI::MathOperator math_operator : GUI::MATH_OPERATORS) {
        CHECK(GUI::ParseOperatorSymbol(GUI::GetOperatorSymbol(math_operator)) == math_operator);
    }
    CHECK_FALSE(GUI::ParseOperatorSymbol("%"));
}
