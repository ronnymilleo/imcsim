/**
 * @file    math_channel.cpp
 * @brief   Compiles a math channel to the program that evaluates it.
 */

#include "results/math_channel.h"

#include <utility>

namespace GUI {

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

} // namespace GUI
