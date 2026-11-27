/**
 * @file    trace_math_parser.h
 * @brief   Compiles the expressions of math channels, such as V(1)-V(OUT), into programs that can run on any result.
 */

#ifndef IMCSIM_TRACE_MATH_PARSER_H
#define IMCSIM_TRACE_MATH_PARSER_H

#include "schematic/math_channel_definition.h"
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  MathInstruction
 * @brief   One step of a compiled expression, run on a stack of traces.
 */
struct MathInstruction {
    /**
     * @enum    Kind
     * @brief   What a step does: push a number, a voltage or a current on the stack, or apply an operation to the
     *          traces on top of it.
     */
    enum class Kind {
        NUMBER,
        VOLTAGE,
        CURRENT,
        NEGATE,
        ADD,
        SUBTRACT,
        MULTIPLY,
        DIVIDE,
        ABS,
        SQUARE_ROOT,
        DERIVATIVE,
        INTEGRAL
    };

    Kind Type = Kind::NUMBER;
    double Number = 0.0;
    int Node = 0;
    std::string Net; ///< A voltage named by a net label, such as OUT, until ResolveNetNames() turns it into a node.
    std::string Current;
};

/**
 * @struct  MathProgram
 * @brief   An expression compiled to instructions in postfix order, ready to evaluate over any result.
 */
struct MathProgram {
    std::vector<MathInstruction> Instructions;
};

/// Finds the node of a net name, or no value when no label gives that name.
using NetResolver = std::function<std::optional<int>(std::string_view)>;

// Writing
std::string BuildOperatorExpression(std::string_view first, MathOperator math_operator, std::string_view second);

// Compiling
std::expected<MathProgram, std::string> CompileExpression(std::string_view text);
std::expected<MathProgram, std::string> ResolveNetNames(MathProgram program, const NetResolver &find_node);

} // namespace GUI

#endif // IMCSIM_TRACE_MATH_PARSER_H
