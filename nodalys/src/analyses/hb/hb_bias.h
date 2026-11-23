/**
 * @file    hb_bias.h
 * @brief   The DC bias that a harmonic balance starts from, and the gmin ladder its solves step down.
 * @details The balance is a Newton iteration, so it needs a state to start from; the DC solve of the circuit with the
 *          DC part of the excitation gives one, with the same fallbacks as an operating point.
 */

#ifndef NODALYS_HB_BIAS_H
#define NODALYS_HB_BIAS_H

#include "analyses/hb/hb_workspace.h"
#include "newton/newton.h"
#include <vector>

namespace Nodalys {

/// Conductance in siemens that the first rung of the gmin ladder adds to every node.
constexpr double GMIN_START = 1e-3;
/// How much each rung of the gmin ladder divides the conductance by.
constexpr double GMIN_FACTOR = 0.1;

// Gmin ladder
double GminAtStep(int step, const ConvergenceOptions &options);

// DC bias
[[nodiscard]] bool
SolveDcBiasForB0(const HbWorkspace &ws, const ConvergenceOptions &options, std::vector<double> &x_dc);

} // namespace Nodalys

#endif // NODALYS_HB_BIAS_H
