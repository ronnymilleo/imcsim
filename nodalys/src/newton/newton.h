/**
 * @file    newton.h
 * @brief   Newton-Raphson nonlinear solver iteration loop and convergence outcomes.
 */

#ifndef NODALYS_NEWTON_H
#define NODALYS_NEWTON_H

#include "devices/devices.h"
#include "nodalys/analyses.h"
#include <memory>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  NewtonOutcome
 * @brief   What a Newton solve ended with, for diagnostic messages.
 */
struct NewtonOutcome {
    bool Converged = false;
    int Iterations = 0;
    /// What recovered a difficult circuit: "gmin stepping" or "source stepping", empty for a plain Newton solve.
    std::string Strategy;
};

/**
 * @brief   Finds the DC solution of a compiled circuit with Newton-Raphson.
 * @param[in,out] sys      The compiled system; devices stamp into it.
 * @param[in,out] devices  The device list of the circuit; holds and limit states.
 * @param[in,out] x        Initial guess in, converged solution out.
 * @param[in]     time     TimeContext indicating DC or transient operating conditions.
 * @param[in]     options  Convergence tolerances and limits.
 * @return  Outcome with iteration count and whether it converged.
 */
NewtonOutcome SolveNewton(MnaSystem &sys, const std::vector<std::unique_ptr<Device>> &devices, std::vector<double> &x,
                          const TimeContext &time, const ConvergenceOptions &options);

} // namespace Nodalys

#endif // NODALYS_NEWTON_H
