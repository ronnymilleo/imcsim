/**
 * @file    math_channel.h
 * @brief   What a math channel computes, as the schematic keeps and saves it, and its compiled program.
 */

#ifndef IMCSIM_MATH_CHANNEL_H
#define IMCSIM_MATH_CHANNEL_H

#include "trace_math.h"
#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

// The operations an operator channel offers, in the order of their buttons
constexpr auto MATH_OPERATORS = std::to_array<MathOperator>({
    MathOperator::ADD,
    MathOperator::SUBTRACT,
    MathOperator::MULTIPLY,
    MathOperator::DIVIDE,
});

/**
 * @struct  MathChannel
 * @brief   A trace computed from others, like the math channel of an oscilloscope.
 * @details Either an operation between two traces, picked with buttons, or a typed expression; both compile to
 *          the same program. Voltages may name a net, such as V(OUT), so a saved channel survives edits that number
 *          the nodes again.
 */
struct MathChannel {
    std::string Name;
    bool Shown = true;
    bool UsesExpression = false;
    std::string First;
    MathOperator Operator = MathOperator::SUBTRACT;
    std::string Second;
    std::string Expression;

    bool operator==(const MathChannel &) const = default;
};

std::string GetChannelExpression(const MathChannel &channel);
std::expected<MathProgram, std::string> CompileChannel(const MathChannel &channel, const NetResolver &find_node);
std::string GetNextChannelName(const std::vector<MathChannel> &channels);
std::optional<MathOperator> ParseOperatorSymbol(std::string_view symbol);

} // namespace GUI

#endif // IMCSIM_MATH_CHANNEL_H
