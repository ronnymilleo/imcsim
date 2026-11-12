/**
 * @file    math_channel.h
 * @brief   Compiles a math channel to the program that evaluates it.
 */

#ifndef IMCSIM_MATH_CHANNEL_H
#define IMCSIM_MATH_CHANNEL_H

#include "results/trace_math.h"
#include "schematic/math_channel_definition.h"
#include <expected>
#include <string>

namespace GUI {

std::string GetChannelExpression(const MathChannel &channel);
std::expected<MathProgram, std::string> CompileChannel(const MathChannel &channel, const NetResolver &find_node);

} // namespace GUI

#endif // IMCSIM_MATH_CHANNEL_H
