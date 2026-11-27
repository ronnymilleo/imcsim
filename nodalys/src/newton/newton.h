/**
 * @file    newton.h
 * @brief   Newton-Raphson nonlinear solver iteration loop and convergence outcomes.
 */

#ifndef NODALYS_NEWTON_H
#define NODALYS_NEWTON_H

#include "compiler/compiled_circuit.h"
#include "nodalys/analyses.h"
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @enum    NodesetPass
 * @brief   Which of the passes of a solve with nodesets is being run.
 */
enum class NodesetPass {
    PULLED,   ///< The nodes are pulled to their guesses.
    RELEASED, ///< The nodesets are let go, starting from the point the pulled pass found.
    PLAIN,    ///< No nodeset acts, as a solve of a circuit without any.
};

/**
 * @struct  NewtonOutcome
 * @brief   What a Newton solve ended with, for diagnostic messages.
 */
struct NewtonOutcome {
    bool Converged = false;
    int Iterations = 0;
    /// What recovered a difficult circuit: "gmin stepping" or "source stepping", empty for a plain Newton solve.
    std::string Strategy;
    /// Why a device could not be stamped at some iterate, such as a behavioral source whose expression has no value
    /// there; empty when none ever failed. It is the likely cause when the solve did not converge.
    std::string Fault;
};

// Clamps and nonlinear solve
void StampNodeClamps(MnaSystem &sys, std::span<const NodeVoltageDirective> clamps);
NewtonOutcome SolveNewton(CompiledCircuit &compiled,
                          std::vector<double> &x,
                          const TimeContext &time,
                          const ConvergenceOptions &options);
bool SolveInNodesetPasses(std::vector<double> &x, const std::function<bool(std::vector<double> &, NodesetPass)> &solve);
std::string DescribeFailure(std::string message, const NewtonOutcome &outcome);

} // namespace Nodalys

#endif // NODALYS_NEWTON_H
