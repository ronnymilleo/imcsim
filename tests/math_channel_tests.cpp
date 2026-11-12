/**
 * @file    math_channel_tests.cpp
 * @brief   Tests for compiling a math channel to the program that evaluates it.
 */

#include "results/math_channel.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>

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
