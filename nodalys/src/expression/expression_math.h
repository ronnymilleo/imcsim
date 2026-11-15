/**
 * @file    expression_math.h
 * @brief   The value and the partial derivatives of every operator and function of an expression, as ngspice's
 *          behavioral source defines them.
 */

#ifndef NODALYS_EXPRESSION_MATH_H
#define NODALYS_EXPRESSION_MATH_H

#include "expression/expression_program.h"
#include <expected>
#include <string>
#include <string_view>

namespace Nodalys {

/// Largest magnitude ngspice lets a function return where the true value is not finite: exp overflows to it, and the
/// logarithm of 0 is minus it.
inline constexpr double LARGEST_VALUE = 1e99;
/// What a division by zero multiplies the dividend by, as in ngspice: 1/0 reads 1e32 and 0/0 reads 0.
inline constexpr double DIVISION_BY_ZERO_FACTOR = 1e32;
/// The slope used where the true one is not finite, as the square root has at 0. It must stand for a steep slope and
/// still leave the matrix solvable: the factorization refuses a pivot smaller than 1e-30 of the largest entry, so a
/// slope of 1e32 would make every conductance of the circuit look like 0.
inline constexpr double UNBOUNDED_SLOPE = 1e12;

/**
 * @struct  OperationResult
 * @brief   What an operation gives from its operands, and its derivative with respect to each of them.
 */
struct OperationResult {
    double Value = 0.0;
    double PartialFirst = 0.0;
    double PartialSecond = 0.0;
};

// Evaluation
std::expected<OperationResult, std::string> ApplyOperation(Operation operation, double first, double second);

// Naming
std::string_view GetOperationName(Operation operation);

} // namespace Nodalys

#endif // NODALYS_EXPRESSION_MATH_H
