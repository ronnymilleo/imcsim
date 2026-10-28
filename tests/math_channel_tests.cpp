/**
 * @file    math_channel_tests.cpp
 * @brief   Tests for what a math channel computes, as the schematic keeps and saves it, and its compiled program.
 */

#include "math_channel.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("A new channel is numbered after the highest of those there are", "[math_channel]") {
    CHECK(GUI::GetNextChannelName({}) == "M1");
    // A gap left by a removed channel is not filled, and names that are not M and a number are ignored
    const std::vector<GUI::MathChannel> channels = {{.Name = "M1"}, {.Name = "M4"}, {.Name = "Mx"}, {.Name = "Power"}};
    CHECK(GUI::GetNextChannelName(channels) == "M5");
}

TEST_CASE("Operations read back from the symbols they are written with", "[math_channel]") {
    for (const GUI::MathOperator math_operator : GUI::MATH_OPERATORS) {
        CHECK(GUI::ParseOperatorSymbol(GUI::GetOperatorSymbol(math_operator)) == math_operator);
    }
    CHECK_FALSE(GUI::ParseOperatorSymbol("%"));
}

TEST_CASE("A channel compiles its operation or its expression, with its nets on the nodes of now", "[math_channel]") {
    const GUI::NetResolver find_node = [](const std::string_view net) -> std::optional<int> {
        return net == "OUT" ? std::optional<int>(3) : std::nullopt;
    };
    GUI::MathChannel channel{.Name = "M1", .First = "V(OUT)", .Operator = GUI::MathOperator::ADD, .Second = "V(1)"};
    CHECK(GUI::GetChannelExpression(channel) == "V(OUT)+V(1)");
    const auto program = GUI::CompileChannel(channel, find_node);
    REQUIRE(program);
    CHECK(program->Instructions.front().Node == 3);

    // The expression wins once the channel uses it; a net the circuit lost is an error on the channel
    channel.UsesExpression = true;
    channel.Expression = "V(IN)";
    const auto lost = GUI::CompileChannel(channel, find_node);
    REQUIRE_FALSE(lost);
    CHECK(lost.error().contains("IN"));
}
