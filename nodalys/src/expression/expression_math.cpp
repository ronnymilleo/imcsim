/**
 * @file    expression_math.cpp
 * @brief   The value and the partial derivatives of every operator and function of an expression, as ngspice's
 *          behavioral source defines them.
 */

#include "expression/expression_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

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

bool IsInRange(const Operation operation, const Operation first, const Operation last) {
    return operation >= first && operation <= last;
}

Result ApplyElementary(const Operation operation, const double argument) {
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
    default:
        return Logarithm(operation, argument, 1.0 / std::numbers::ln10);
    }
}

Result ApplyTrigonometric(const Operation operation, const double argument) {
    switch (operation) {
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
    default:
        return OperationResult{.Value = std::atan(argument), .PartialFirst = 1.0 / (1.0 + argument * argument)};
    }
}

Result ApplyHyperbolic(const Operation operation, const double argument) {
    switch (operation) {
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
    default:
        return InverseHyperbolic(operation, argument);
    }
}

Result ApplyShaping(const Operation operation, const double argument) {
    switch (operation) {
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

Result ApplyFunction(const Operation operation, const double argument) {
    if (IsInRange(operation, Operation::ABS, Operation::LOG10)) {
        return ApplyElementary(operation, argument);
    }
    if (IsInRange(operation, Operation::SIN, Operation::ATAN)) {
        return ApplyTrigonometric(operation, argument);
    }
    if (IsInRange(operation, Operation::SINH, Operation::ATANH)) {
        return ApplyHyperbolic(operation, argument);
    }
    return ApplyShaping(operation, argument);
}

Result ApplyArithmetic(const Operation operation, const double first, const double second) {
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
    default:
        return std::unexpected(std::format("{} is not an operation of two operands", GetOperationName(operation)));
    }
}

Result ApplyComparison(const Operation operation, const double first, const double second) {
    switch (operation) {
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
    default:
        return Flat(TruthValue(first != 0.0 || second != 0.0));
    }
}

Result ApplyBinary(const Operation operation, const double first, const double second) {
    if (IsInRange(operation, Operation::LESS, Operation::OR)) {
        return ApplyComparison(operation, first, second);
    }
    return ApplyArithmetic(operation, first, second);
}

constexpr auto OPERATION_NAMES = std::to_array<std::pair<Operation, std::string_view>>({
    {Operation::CONSTANT, "constant"},
    {Operation::INPUT, "input"},
    {Operation::TIME, "time"},
    {Operation::TEMPERATURE, "temper"},
    {Operation::NEGATE, "-"},
    {Operation::NOT, "!"},
    {Operation::ADD, "+"},
    {Operation::SUBTRACT, "-"},
    {Operation::MULTIPLY, "*"},
    {Operation::DIVIDE, "/"},
    {Operation::POWER, "^"},
    {Operation::LESS, "<"},
    {Operation::GREATER, ">"},
    {Operation::LESS_EQUAL, "<="},
    {Operation::GREATER_EQUAL, ">="},
    {Operation::EQUAL, "=="},
    {Operation::NOT_EQUAL, "!="},
    {Operation::AND, "&&"},
    {Operation::OR, "||"},
    {Operation::TERNARY, "?:"},
    {Operation::ABS, "abs"},
    {Operation::SQRT, "sqrt"},
    {Operation::EXP, "exp"},
    {Operation::LN, "ln"},
    {Operation::LOG10, "log10"},
    {Operation::SIN, "sin"},
    {Operation::COS, "cos"},
    {Operation::TAN, "tan"},
    {Operation::ASIN, "asin"},
    {Operation::ACOS, "acos"},
    {Operation::ATAN, "atan"},
    {Operation::SINH, "sinh"},
    {Operation::COSH, "cosh"},
    {Operation::TANH, "tanh"},
    {Operation::ASINH, "asinh"},
    {Operation::ACOSH, "acosh"},
    {Operation::ATANH, "atanh"},
    {Operation::SIGN, "sgn"},
    {Operation::STEP, "u"},
    {Operation::RAMP_STEP, "u2"},
    {Operation::RAMP, "uramp"},
    {Operation::FLOOR, "floor"},
    {Operation::CEIL, "ceil"},
    {Operation::NEAREST_INTEGER, "nint"},
    {Operation::MIN, "min"},
    {Operation::MAX, "max"},
    {Operation::POW, "pow"},
    {Operation::PWR, "pwr"},
});

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
    if (operation == Operation::NEGATE) {
        return OperationResult{.Value = -first, .PartialFirst = -1.0};
    }
    if (operation == Operation::NOT) {
        return Flat(TruthValue(first == 0.0));
    }
    if (IsInRange(operation, Operation::ADD, Operation::OR) || IsInRange(operation, Operation::MIN, Operation::PWR)) {
        return ApplyBinary(operation, first, second);
    }
    if (IsInRange(operation, Operation::CONSTANT, Operation::TEMPERATURE) || operation == Operation::TERNARY) {
        return std::unexpected(std::format("{} is not an operation on values", GetOperationName(operation)));
    }
    return ApplyFunction(operation, first);
}

/**
 * @brief   Names an operation as it is written in an expression, for the messages.
 * @param[in] operation  Any operation.
 * @return  The operator or function name, such as "sqrt" or "^".
 */
std::string_view GetOperationName(const Operation operation) {
    for (const auto &[entry, name] : OPERATION_NAMES) {
        if (entry == operation) {
            return name;
        }
    }
    return "operation";
}

} // namespace Nodalys
