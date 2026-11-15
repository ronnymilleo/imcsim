/**
 * @file    math_channel_definition.cpp
 * @brief   What a math channel computes, as the schematic keeps and saves it.
 */

#include "schematic/math_channel_definition.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace GUI {

namespace {

constexpr std::string_view CHANNEL_PREFIX = "M";

} // namespace

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
 * @brief   Returns how an operation is typed in an expression.
 * @param[in] math_operator  Operation.
 * @return  "+", "-", "*" or "/".
 */
const char *GetOperatorSymbol(const MathOperator math_operator) {
    switch (math_operator) {
    case MathOperator::ADD:
        return "+";
    case MathOperator::SUBTRACT:
        return "-";
    case MathOperator::MULTIPLY:
        return "*";
    case MathOperator::DIVIDE:
        return "/";
    }
    return "+";
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
