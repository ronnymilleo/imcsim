/**
 * @file    trace_math.h
 * @brief   Math channels: expressions of voltages and currents, such as V(1)-V(OUT), evaluated over a result.
 * @details The interface of the topic, which one module each of its parts stands behind: the parser compiles the
 *          expressions, the units name what they compute, and the evaluator runs them over transient, DC sweep and AC
 *          results.
 */

#ifndef IMCSIM_TRACE_MATH_H
#define IMCSIM_TRACE_MATH_H

#include "results/trace_math_eval.h"
#include "results/trace_math_parser.h"
#include "results/trace_math_units.h"

#endif // IMCSIM_TRACE_MATH_H
