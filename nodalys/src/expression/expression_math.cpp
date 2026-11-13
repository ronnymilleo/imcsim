/**
 * @file    expression_math.cpp
 * @brief   The value and the partial derivatives of every operator and function of an expression, as ngspice's
 *          behavioral source defines them.
 */

#include "expression/expression_math.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace Nodalys {

namespace {

using Result = std::expected<OperationResult, std::string>;

double Sign(const double value) {
    if (value > 0.0) {
        return 1.0;
    }
    return value < 0.0 ? -1.0 : 0.0;
}

bool IsInteger(const double value) {
    return value == std::trunc(value);
}

double TruthValue(const bool is_true) {
    return is_true ? 1.0 : 0.0;
}

/// A slope as a quotient, where a divisor of 0 gives the unbounded slope, so the first iteration of a Newton solve,
/// where every node is still at 0 V, can linearize a square root or a logarithm of a node voltage.
double SlopeOfQuotient(const double dividend, const double divisor) {
    return divisor == 0.0 ? UNBOUNDED_SLOPE : dividend / divisor;
}

Result OutOfRange(const Operation operation, const double first) {
    return std::unexpected(std::format("{}({}) is out of range", GetOperationName(operation), first));
}

Result OutOfRange(const Operation operation, const double first, const double second) {
    return std::unexpected(std::format("{}({}, {}) is out of range", GetOperationName(operation), first, second));
}

OperationResult Divide(const double dividend, const double divisor) {
    if (divisor == 0.0) {
        const double slope = dividend == 0.0 ? 0.0 : UNBOUNDED_SLOPE;
        return {.Value = dividend * DIVISION_BY_ZERO_FACTOR, .PartialFirst = slope, .PartialSecond = -slope};
    }
    const double quotient = dividend / divisor;
    return {.Value = quotient, .PartialFirst = 1.0 / divisor, .PartialSecond = -quotient / divisor};
}

/// Derivative of the power with respect to the base at a base of 0, where it is 1, 0 or without bound by the exponent.
double SlopeOfPowerAtZero(const double exponent) {
    if (exponent == 1.0) {
        return 1.0;
    }
    return exponent > 1.0 ? 0.0 : UNBOUNDED_SLOPE;
}

/**
 * @brief   The operator `^` and `**`: an exact power for a positive base or an integer exponent, 0 for a negative base
 *          with a fractional exponent, which a real number does not have, and 0^0 = 1.
 */
Result Power(const double base, const double exponent) {
    if (base == 0.0) {
        if (exponent == 0.0) {
            return OperationResult{.Value = 1.0};
        }
        if (exponent < 0.0) {
            return OutOfRange(Operation::POWER, base, exponent);
        }
        return OperationResult{.Value = 0.0, .PartialFirst = SlopeOfPowerAtZero(exponent)};
    }
    if (base < 0.0) {
        if (!IsInteger(exponent)) {
            return OperationResult{};
        }
        return OperationResult{.Value = std::pow(base, exponent),
                               .PartialFirst = exponent * std::pow(base, exponent - 1.0)};
    }
    const double value = std::pow(base, exponent);
    return OperationResult{.Value = value,
                           .PartialFirst = exponent * std::pow(base, exponent - 1.0),
                           .PartialSecond = value * std::log(base)};
}

/**
 * @brief   The function pow(): the operator, except that a base of 0 gives 0 for every exponent.
 */
Result PowerFunction(const double base, const double exponent) {
    if (base == 0.0) {
        return OperationResult{.Value = 0.0, .PartialFirst = exponent > 0.0 ? SlopeOfPowerAtZero(exponent) : 0.0};
    }
    return Power(base, exponent);
}

/**
 * @brief   The function pwr(): the magnitude to the exponent with the sign of the base, so it is odd in the base.
 */
Result SignedPower(const double base, const double exponent) {
    if (base == 0.0) {
        if (exponent == 0.0) {
            return OperationResult{.Value = 1.0};
        }
        if (exponent < 0.0) {
            return OutOfRange(Operation::PWR, base, exponent);
        }
        // ngspice reads the slope of pwr() at 0 as 0, whatever the exponent
        return OperationResult{.Value = 0.0};
    }
    const double sign = Sign(base);
    const double magnitude = std::fabs(base);
    const double value = sign * std::pow(magnitude, exponent);
    return OperationResult{.Value = value,
                           .PartialFirst = exponent * std::pow(magnitude, exponent - 1.0),
                           .PartialSecond = value * std::log(magnitude)};
}

Result Logarithm(const Operation operation, const double argument, const double scale) {
    if (argument < 0.0) {
        return OutOfRange(operation, argument);
    }
    if (argument == 0.0) {
        return OperationResult{.Value = -LARGEST_VALUE, .PartialFirst = UNBOUNDED_SLOPE};
    }
    return OperationResult{.Value = std::log(argument) * scale, .PartialFirst = scale / argument};
}

Result InverseTrigonometric(const Operation operation, const double argument) {
    if (std::fabs(argument) > 1.0) {
        return OutOfRange(operation, argument);
    }
    const double slope = SlopeOfQuotient(1.0, std::sqrt(1.0 - argument * argument));
    if (operation == Operation::ASIN) {
        return OperationResult{.Value = std::asin(argument), .PartialFirst = slope};
    }
    return OperationResult{.Value = std::acos(argument), .PartialFirst = -slope};
}

Result InverseHyperbolic(const Operation operation, const double argument) {
    switch (operation) {
    case Operation::ASINH:
        return OperationResult{.Value = std::asinh(argument),
                               .PartialFirst = 1.0 / std::sqrt(argument * argument + 1.0)};
    case Operation::ACOSH:
        if (argument < 1.0) {
            return OutOfRange(operation, argument);
        }
        return OperationResult{.Value = std::acosh(argument),
                               .PartialFirst = SlopeOfQuotient(1.0, std::sqrt(argument * argument - 1.0))};
    case Operation::ATANH:
    default:
        if (std::fabs(argument) >= 1.0) {
            return OutOfRange(operation, argument);
        }
        return OperationResult{.Value = std::atanh(argument), .PartialFirst = 1.0 / (1.0 - argument * argument)};
    }
}

/// A function whose slope is zero wherever it is defined: the steps, which jump, and the roundings.
OperationResult Flat(const double value) {
    return OperationResult{.Value = value};
}

Result ApplyFunction(const Operation operation, const double argument) {
    switch (operation) {
    case Operation::ABS:
        return OperationResult{.Value = std::fabs(argument), .PartialFirst = Sign(argument)};
    case Operation::SQRT:
        if (argument < 0.0) {
            return OutOfRange(operation, argument);
        }
        return OperationResult{.Value = std::sqrt(argument), .PartialFirst = SlopeOfQuotient(0.5, std::sqrt(argument))};
    case Operation::EXP: {
        const double value = std::exp(argument);
        // Past the largest value the function is flat, which is what the clamp makes of it
        return value >= LARGEST_VALUE ? OperationResult{.Value = LARGEST_VALUE}
                                      : OperationResult{.Value = value, .PartialFirst = value};
    }
    case Operation::LN:
        return Logarithm(operation, argument, 1.0);
    case Operation::LOG10:
        return Logarithm(operation, argument, 1.0 / std::numbers::ln10);
    case Operation::SIN:
        return OperationResult{.Value = std::sin(argument), .PartialFirst = std::cos(argument)};
    case Operation::COS:
        return OperationResult{.Value = std::cos(argument), .PartialFirst = -std::sin(argument)};
    case Operation::TAN: {
        const double value = std::tan(argument);
        return OperationResult{.Value = value, .PartialFirst = 1.0 + value * value};
    }
    case Operation::ASIN:
    case Operation::ACOS:
        return InverseTrigonometric(operation, argument);
    case Operation::ATAN:
        return OperationResult{.Value = std::atan(argument), .PartialFirst = 1.0 / (1.0 + argument * argument)};
    case Operation::SINH:
        return OperationResult{.Value = std::sinh(argument), .PartialFirst = std::cosh(argument)};
    case Operation::COSH:
        return OperationResult{.Value = std::cosh(argument), .PartialFirst = std::sinh(argument)};
    case Operation::TANH: {
        const double value = std::tanh(argument);
        return OperationResult{.Value = value, .PartialFirst = 1.0 - value * value};
    }
    case Operation::ASINH:
    case Operation::ACOSH:
    case Operation::ATANH:
        return InverseHyperbolic(operation, argument);
    case Operation::SIGN:
        return Flat(Sign(argument));
    case Operation::STEP:
        // ngspice's u(0) is the middle of the jump
        return Flat(argument == 0.0 ? 0.5 : TruthValue(argument > 0.0));
    case Operation::RAMP_STEP: {
        const bool is_corner = argument == 0.0 || argument == 1.0;
        const double slope = is_corner ? 0.5 : TruthValue(argument > 0.0 && argument < 1.0);
        return OperationResult{.Value = std::clamp(argument, 0.0, 1.0), .PartialFirst = slope};
    }
    case Operation::RAMP:
        return OperationResult{.Value = std::max(argument, 0.0),
                               .PartialFirst = argument == 0.0 ? 0.5 : TruthValue(argument > 0.0)};
    case Operation::FLOOR:
        return Flat(std::floor(argument));
    case Operation::CEIL:
        return Flat(std::ceil(argument));
    case Operation::NEAREST_INTEGER:
        // Halves round to the even integer, as the C library does and ngspice with it
        return Flat(std::nearbyint(argument));
    default:
        return std::unexpected(std::format("{} is not a function of one operand", GetOperationName(operation)));
    }
}

Result ApplyBinary(const Operation operation, const double first, const double second) {
    switch (operation) {
    case Operation::ADD:
        return OperationResult{.Value = first + second, .PartialFirst = 1.0, .PartialSecond = 1.0};
    case Operation::SUBTRACT:
        return OperationResult{.Value = first - second, .PartialFirst = 1.0, .PartialSecond = -1.0};
    case Operation::MULTIPLY:
        return OperationResult{.Value = first * second, .PartialFirst = second, .PartialSecond = first};
    case Operation::DIVIDE:
        return Divide(first, second);
    case Operation::POWER:
        return Power(first, second);
    case Operation::POW:
        return PowerFunction(first, second);
    case Operation::PWR:
        return SignedPower(first, second);
    case Operation::MIN:
        return first < second ? OperationResult{.Value = first, .PartialFirst = 1.0}
                              : OperationResult{.Value = second, .PartialSecond = 1.0};
    case Operation::MAX:
        return first > second ? OperationResult{.Value = first, .PartialFirst = 1.0}
                              : OperationResult{.Value = second, .PartialSecond = 1.0};
    case Operation::LESS:
        return Flat(TruthValue(first < second));
    case Operation::GREATER:
        return Flat(TruthValue(first > second));
    case Operation::LESS_EQUAL:
        return Flat(TruthValue(first <= second));
    case Operation::GREATER_EQUAL:
        return Flat(TruthValue(first >= second));
    case Operation::EQUAL:
        return Flat(TruthValue(first == second));
    case Operation::NOT_EQUAL:
        return Flat(TruthValue(first != second));
    case Operation::AND:
        return Flat(TruthValue(first != 0.0 && second != 0.0));
    case Operation::OR:
        return Flat(TruthValue(first != 0.0 || second != 0.0));
    default:
        return std::unexpected(std::format("{} is not an operation of two operands", GetOperationName(operation)));
    }
}

} // namespace

/**
 * @brief   Computes one operator or function and its derivatives.
 * @details The derivative is the exact one wherever the function has it. The jumps and corners, which are the steps,
 *          the comparisons, `abs`, `min`, `max` and the roundings, take the slope of the side the value is on, so no
 *          smoothing changes a result. On a jump itself the slope is 0; on the corner of `abs`, `uramp` and `u2` it is
 *          the mean of the two sides, and a tie of `min` or `max` takes the second operand, which is what ngspice does.
 * At a point where the true slope is not finite, as `sqrt` at 0, the slope is a large finite number, the way ngspice
 * protects a divide by zero, so the first Newton iteration, which starts from 0 V everywhere, can still be linearized.
 * @param[in] operation  The operator or function; a leaf or a ternary is not an operation on values.
 * @param[in] first      First operand, or the only one.
 * @param[in] second     Second operand, ignored by the functions of one.
 * @return  The value and the two partial derivatives, or why the operands are out of range, such as `sqrt(-1)` or
 *          `ln(-2)`, which ngspice refuses too.
 */
std::expected<OperationResult, std::string> ApplyOperation(const Operation operation, const double first,
                                                           const double second) {
    switch (operation) {
    case Operation::NEGATE:
        return OperationResult{.Value = -first, .PartialFirst = -1.0};
    case Operation::NOT:
        return Flat(TruthValue(first == 0.0));
    case Operation::ADD:
    case Operation::SUBTRACT:
    case Operation::MULTIPLY:
    case Operation::DIVIDE:
    case Operation::POWER:
    case Operation::POW:
    case Operation::PWR:
    case Operation::MIN:
    case Operation::MAX:
    case Operation::LESS:
    case Operation::GREATER:
    case Operation::LESS_EQUAL:
    case Operation::GREATER_EQUAL:
    case Operation::EQUAL:
    case Operation::NOT_EQUAL:
    case Operation::AND:
    case Operation::OR:
        return ApplyBinary(operation, first, second);
    case Operation::CONSTANT:
    case Operation::INPUT:
    case Operation::TIME:
    case Operation::TEMPERATURE:
    case Operation::TERNARY:
        return std::unexpected(std::format("{} is not an operation on values", GetOperationName(operation)));
    case Operation::ABS:
    case Operation::SQRT:
    case Operation::EXP:
    case Operation::LN:
    case Operation::LOG10:
    case Operation::SIN:
    case Operation::COS:
    case Operation::TAN:
    case Operation::ASIN:
    case Operation::ACOS:
    case Operation::ATAN:
    case Operation::SINH:
    case Operation::COSH:
    case Operation::TANH:
    case Operation::ASINH:
    case Operation::ACOSH:
    case Operation::ATANH:
    case Operation::SIGN:
    case Operation::STEP:
    case Operation::RAMP_STEP:
    case Operation::RAMP:
    case Operation::FLOOR:
    case Operation::CEIL:
    case Operation::NEAREST_INTEGER:
    default:
        return ApplyFunction(operation, first);
    }
}

/**
 * @brief   Names an operation as it is written in an expression, for the messages.
 * @param[in] operation  Any operation.
 * @return  The operator or function name, such as "sqrt" or "^".
 */
std::string_view GetOperationName(const Operation operation) {
    switch (operation) {
    case Operation::CONSTANT:
        return "constant";
    case Operation::INPUT:
        return "input";
    case Operation::TIME:
        return "time";
    case Operation::TEMPERATURE:
        return "temper";
    case Operation::NEGATE:
    case Operation::SUBTRACT:
        return "-";
    case Operation::NOT:
        return "!";
    case Operation::ADD:
        return "+";
    case Operation::MULTIPLY:
        return "*";
    case Operation::DIVIDE:
        return "/";
    case Operation::POWER:
        return "^";
    case Operation::LESS:
        return "<";
    case Operation::GREATER:
        return ">";
    case Operation::LESS_EQUAL:
        return "<=";
    case Operation::GREATER_EQUAL:
        return ">=";
    case Operation::EQUAL:
        return "==";
    case Operation::NOT_EQUAL:
        return "!=";
    case Operation::AND:
        return "&&";
    case Operation::OR:
        return "||";
    case Operation::TERNARY:
        return "?:";
    case Operation::ABS:
        return "abs";
    case Operation::SQRT:
        return "sqrt";
    case Operation::EXP:
        return "exp";
    case Operation::LN:
        return "ln";
    case Operation::LOG10:
        return "log10";
    case Operation::SIN:
        return "sin";
    case Operation::COS:
        return "cos";
    case Operation::TAN:
        return "tan";
    case Operation::ASIN:
        return "asin";
    case Operation::ACOS:
        return "acos";
    case Operation::ATAN:
        return "atan";
    case Operation::SINH:
        return "sinh";
    case Operation::COSH:
        return "cosh";
    case Operation::TANH:
        return "tanh";
    case Operation::ASINH:
        return "asinh";
    case Operation::ACOSH:
        return "acosh";
    case Operation::ATANH:
        return "atanh";
    case Operation::SIGN:
        return "sgn";
    case Operation::STEP:
        return "u";
    case Operation::RAMP_STEP:
        return "u2";
    case Operation::RAMP:
        return "uramp";
    case Operation::FLOOR:
        return "floor";
    case Operation::CEIL:
        return "ceil";
    case Operation::NEAREST_INTEGER:
        return "nint";
    case Operation::MIN:
        return "min";
    case Operation::MAX:
        return "max";
    case Operation::POW:
        return "pow";
    case Operation::PWR:
        return "pwr";
    default:
        return "operation";
    }
}

} // namespace Nodalys
