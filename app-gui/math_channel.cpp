/**
 * @file    math_channel.cpp
 * @brief   What a math channel computes, as the schematic keeps and saves it, and its compiled program.
 */

#include "math_channel.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr std::string_view CHANNEL_PREFIX = "M";

} // namespace

/**
 * @brief   Writes what a channel computes as one expression.
 * @param[in] channel  Channel with an operation or a typed expression.
 * @return  The typed expression, or the operation written as one, such as V(1)-V(OUT).
 */
std::string GetChannelExpression(const MathChannel &channel) {
    if (channel.UsesExpression) {
        return channel.Expression;
    }
    return BuildOperatorExpression(channel.First, channel.Operator, channel.Second);
}

/**
 * @brief   Compiles a channel for the circuit as it is now.
 * @param[in] channel    Channel to compile.
 * @param[in] find_node  Finds the node of a net name.
 * @return  The program, with every voltage on a node number, or why the expression does not compile or which net
 *          the circuit no longer has.
 */
std::expected<MathProgram, std::string> CompileChannel(const MathChannel &channel, const NetResolver &find_node) {
    std::expected<MathProgram, std::string> program = CompileExpression(GetChannelExpression(channel));
    if (!program) {
        return program;
    }
    return ResolveNetNames(std::move(*program), find_node);
}

/**
 * @brief   Names a new channel after those there are.
 * @param[in] channels  Channels already there.
 * @return  "M" and one more than the highest number of the names like M1, M2..., so "M1" for the first channel.
 */
std::string GetNextChannelName(const std::vector<MathChannel> &channels) {
    int highest = 0;
    for (const MathChannel &channel : channels) {
        const std::string_view name = channel.Name;
        if (!name.starts_with(CHANNEL_PREFIX)) {
            continue;
        }
        int number = 0;
        const char *const end = name.data() + name.size();
        const auto [last, error] = std::from_chars(name.data() + CHANNEL_PREFIX.size(), end, number);
        if (error == std::errc() && last == end) {
            highest = std::max(highest, number);
        }
    }
    return std::format("{}{}", CHANNEL_PREFIX, highest + 1);
}

/**
 * @brief   Reads an operation as it is typed.
 * @param[in] symbol  "+", "-", "*" or "/".
 * @return  The operation, or no value for any other text.
 */
std::optional<MathOperator> ParseOperatorSymbol(const std::string_view symbol) {
    const auto found = std::ranges::find_if(MATH_OPERATORS, [symbol](const MathOperator math_operator) {
        return symbol == GetOperatorSymbol(math_operator);
    });
    if (found == MATH_OPERATORS.end()) {
        return std::nullopt;
    }
    return *found;
}

} // namespace GUI
