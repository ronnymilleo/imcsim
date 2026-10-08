/**
 * @file    trace_math.h
 * @brief   Math channels: expressions of voltages and currents, such as V(1)-V(2), evaluated over a result.
 */

#ifndef IMCSIM_TRACE_MATH_H
#define IMCSIM_TRACE_MATH_H

#include "simulator.h"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  Dimension
 * @brief   Physical unit of a value, as powers of volts, amperes and seconds, so V*I reads as watts.
 * @details Adding values of different units gives no known unit; such a result is still plotted, without one.
 */
struct Dimension {
    int Volts = 0;
    int Amperes = 0;
    int Seconds = 0;
    bool Known = true;

    bool operator==(const Dimension &) const = default;
};

/**
 * @enum    MathOperator
 * @brief   The operations of the math channel of an oscilloscope, between two traces.
 */
enum class MathOperator {
    Add,
    Subtract,
    Multiply,
    Divide
};

/**
 * @struct  MathInstruction
 * @brief   One step of a compiled expression, run on a stack of traces.
 */
struct MathInstruction {
    enum class Kind {
        Number,
        Voltage,
        Current,
        Negate,
        Add,
        Subtract,
        Multiply,
        Divide,
        Absolute,
        SquareRoot,
        Derivative,
        Integral
    };

    Kind Type = Kind::Number;
    double Number = 0.0;
    int Node = 0;
    std::string Current;
};

/**
 * @struct  MathProgram
 * @brief   An expression compiled to instructions in postfix order, ready to evaluate over any result.
 */
struct MathProgram {
    std::vector<MathInstruction> Instructions;
};

/**
 * @struct  MathResult
 * @brief   An evaluated expression: one value per sample of the result, and their unit.
 */
struct MathResult {
    std::vector<double> Values;
    Dimension Unit;
};

std::expected<MathProgram, std::string> CompileExpression(std::string_view text);
std::expected<MathResult, std::string> EvaluateExpression(const MathProgram &program, std::span<const double> xs,
                                                          Dimension x_unit,
                                                          const std::vector<std::vector<double>> &node_voltages,
                                                          const std::vector<Core::ComponentTrace> &currents);
std::string BuildOperatorExpression(std::string_view first, MathOperator math_operator, std::string_view second);
std::string FormatUnit(Dimension unit);
const char *GetOperatorSymbol(MathOperator math_operator);

} // namespace GUI

#endif // IMCSIM_TRACE_MATH_H
