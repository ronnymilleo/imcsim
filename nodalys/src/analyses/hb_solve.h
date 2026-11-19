/**
 * @file    hb_solve.h
 * @brief   Solves the harmonic balance: a direct solve for a linear circuit, Newton and its fallbacks for a nonlinear
 * one.
 */

#ifndef NODALYS_HB_SOLVE_H
#define NODALYS_HB_SOLVE_H

#include "analyses/hb_workspace.h"
#include "newton/newton.h"
#include "nodalys/analyses.h"
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  HbSolveOutcome
 * @brief   How a harmonic balance ended and what it took.
 */
struct HbSolveOutcome {
    bool Converged = false;
    int Iterations = 0;
    /// The strategy that converged, or empty when none did.
    std::string Strategy;
};

// Linear circuit
[[nodiscard]] bool SolveLinearCircuit(const HbWorkspace &ws, std::vector<double> &coeffs_out);

// Nonlinear circuit
HbSolveOutcome SolveHarmonicBalanceNonlinear(HbWorkspace &ws,
                                             const HarmonicBalanceConfig &config,
                                             const ConvergenceOptions &options,
                                             std::vector<double> &x_hb);

} // namespace Nodalys

#endif // NODALYS_HB_SOLVE_H
