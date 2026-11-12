/**
 * @file    math_channel_definition.h
 * @brief   What a math channel computes, as the schematic keeps and saves it.
 */

#ifndef IMCSIM_MATH_CHANNEL_DEFINITION_H
#define IMCSIM_MATH_CHANNEL_DEFINITION_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @enum    MathOperator
 * @brief   The operations of the math channel of an oscilloscope, between two traces.
 */
enum class MathOperator {
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE
};

/// The operations an operator channel offers, in the order of their buttons.
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

// Naming
std::string GetNextChannelName(const std::vector<MathChannel> &channels);

// Operations
const char *GetOperatorSymbol(MathOperator math_operator);
std::optional<MathOperator> ParseOperatorSymbol(std::string_view symbol);

} // namespace GUI

#endif // IMCSIM_MATH_CHANNEL_DEFINITION_H
