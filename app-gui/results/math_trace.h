/**
 * @file    math_trace.h
 * @brief   A math channel evaluated over a result, ready to plot and measure.
 */

#ifndef IMCSIM_MATH_TRACE_H
#define IMCSIM_MATH_TRACE_H

#include "implot.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  MathTrace
 * @brief   A math channel evaluated over one result, ready to plot and measure.
 */
struct MathTrace {
    std::string Label;
    std::vector<double> Values;
    std::string Unit;
    ImAxis Axis = ImAxis_Y1;
    ImU32 Color = 0;
};

/**
 * @struct  ACMathTraces
 * @brief   The math channels evaluated over an AC sweep, one magnitude and one phase trace per channel, in the same
 *          order.
 * @details Each trace keeps the unit of the channel, such as "Ohm", while its values are decibels or degrees.
 */
struct ACMathTraces {
    std::vector<MathTrace> Magnitudes;
    std::vector<MathTrace> Phases;
};

} // namespace GUI

#endif // IMCSIM_MATH_TRACE_H
