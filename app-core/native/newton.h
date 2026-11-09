/**
 * @file    newton.h
 * @brief   Newton-Raphson driver of the native solver: the iteration loop, its convergence test and the fallback
 *          strategies that recover when plain Newton does not converge.
 * @details Theory: Vlach & Singhal, "Computer Methods for Circuit Analysis and Design" (Newton-Raphson for nonlinear
 *          networks, and the source-stepping homotopy), and Kundert, "The Designer's Guide to SPICE and Spectre",
 *          chapter 4 (pn-junction limiting as damping, gmin stepping).
 */

#ifndef IMCSIM_NATIVE_NEWTON_H
#define IMCSIM_NATIVE_NEWTON_H

#include "native/devices.h"
#include <memory>
#include <string>
#include <vector>

namespace Core::Native {

/**
 * @struct  ConvergenceOptions
 * @brief   The tolerances and limits of a Newton solve, mirroring the ngspice defaults.
 */
struct ConvergenceOptions {
    /// Relative tolerance applied to every unknown.
    double RelTol = 1e-3;
    /// Absolute tolerance of the node voltages (ngspice vntol).
    double VoltageAbsTol = 1e-6;
    /// Absolute tolerance of the branch currents (ngspice abstol).
    double CurrentAbsTol = 1e-12;
    /// Most iterations of one Newton attempt before a fallback strategy takes over.
    int MaxIterations = 150;
    /// Conductance from every node to ground, always on, which regularizes floating nodes (ngspice gmin).
    double Gmin = 1e-12;
};

/**
 * @struct  NewtonOutcome
 * @brief   What a Newton solve ended with, for the messages of the run.
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
 * @param[in]     devices  Every device of the circuit, in any order.
 * @param[in,out] x        Initial guess on entry, solution on success; kept as the seed of the next point.
 * @param[in]     options  Tolerances and limits.
 * @param[in]     time     What the stamps are for: a DC point, or a transient point at Time with step TimeStep.
 * @return  Whether a solution was found, with how many iterations and which strategy found it.
 * @note    The fallback strategies, like ngspice's: first plain Newton; then gmin stepping, which solves the circuit
 *          with a large conductance to ground eased off over several runs; then source stepping, which ramps the
 *          independent sources from zero up, reusing each solution as the seed of the next ramp step.
 */
NewtonOutcome SolveDc(MnaSystem &sys, const std::vector<std::unique_ptr<Device>> &devices, std::vector<double> &x,
                      const ConvergenceOptions &options, const TimeContext &time);

} // namespace Core::Native

#endif // IMCSIM_NATIVE_NEWTON_H
