/**
 * @file    expression_program.h
 * @brief   The compact form an expression is parsed to: a flat list of operations that refer to each other by index.
 */

#ifndef NODALYS_EXPRESSION_PROGRAM_H
#define NODALYS_EXPRESSION_PROGRAM_H

#include "nodalys/expression.h"
#include <vector>

namespace Nodalys {

/**
 * @enum    Operation
 * @brief   What a node of an expression computes from its operands.
 */
enum class Operation {
    // Leaves
    CONSTANT,
    INPUT,
    TIME,
    TEMPERATURE,
    // Operators
    NEGATE,
    NOT,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    POWER,
    LESS,
    GREATER,
    LESS_EQUAL,
    GREATER_EQUAL,
    EQUAL,
    NOT_EQUAL,
    AND,
    OR,
    TERNARY,
    // Functions of one operand
    ABS,
    SQRT,
    EXP,
    LN,
    LOG10,
    SIN,
    COS,
    TAN,
    ASIN,
    ACOS,
    ATAN,
    SINH,
    COSH,
    TANH,
    ASINH,
    ACOSH,
    ATANH,
    SIGN,
    STEP,
    RAMP_STEP,
    RAMP,
    FLOOR,
    CEIL,
    NEAREST_INTEGER,
    // Functions of two operands
    MIN,
    MAX,
    POW,
    PWR
};

/**
 * @struct  ExpressionNode
 * @brief   One operation and the nodes it reads, which always come before it in the list.
 */
struct ExpressionNode {
    Operation Kind = Operation::CONSTANT;
    int First = -1; ///< Operands, or -1 where the operation takes fewer.
    int Second = -1;
    int Third = -1;
    double Constant = 0.0; ///< Value of a CONSTANT.
    int Input = -1;        ///< Index into the inputs of an INPUT.
};

/**
 * @struct  ExpressionProgram
 * @brief   The nodes of an expression, the inputs they read and the references of the text.
 */
struct ExpressionProgram {
    std::vector<ExpressionNode> Nodes;
    int Root = -1;                       ///< The node whose value is the expression, the last one.
    std::vector<ExpressionInput> Inputs; ///< Distinct inputs, in the order they first appear.
    std::vector<ExpressionReference> References;
    bool IsTimeDependent = false;
};

} // namespace Nodalys

#endif // NODALYS_EXPRESSION_PROGRAM_H
