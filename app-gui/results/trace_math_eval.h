/**
 * @file    trace_math_eval.h
 * @brief   Evaluates compiled math channels over the samples of a result, or over the phasors of an AC sweep.
 */

#ifndef IMCSIM_TRACE_MATH_EVAL_H
#define IMCSIM_TRACE_MATH_EVAL_H

#include "results/trace_math_parser.h"
#include "results/trace_math_units.h"
#include "simulation/simulator.h"
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  MathResult
 * @brief   An evaluated expression: one value per sample of the result, and their unit.
 */
struct MathResult {
    std::vector<double> Values;
    Dimension Unit;
};

/**
 * @struct  ACMathResult
 * @brief   An expression evaluated over an AC sweep: its magnitude and phase at each frequency, and its unit.
 */
struct ACMathResult {
    std::vector<double> MagnitudesDecibels; ///< Relative to 1 of the unit, as 20*log10 of the magnitude.
    std::vector<double> PhasesDegrees;
    Dimension Unit;
};

// Evaluating
std::expected<MathResult, std::string> EvaluateExpression(const MathProgram &program,
                                                          std::span<const double> xs,
                                                          Dimension x_unit,
                                                          const std::vector<std::vector<double>> &node_voltages,
                                                          const std::vector<Core::ComponentTrace> &currents);
std::expected<ACMathResult, std::string> EvaluateACExpression(const MathProgram &program, const Core::ACSweep &sweep);

} // namespace GUI

#endif // IMCSIM_TRACE_MATH_EVAL_H
