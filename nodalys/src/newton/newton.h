/**
 * @file    newton.h
 * @brief   Newton-Raphson nonlinear solver iteration loop and convergence outcomes.
 */

#ifndef NODALYS_NEWTON_H
#define NODALYS_NEWTON_H

#include "devices/devices.h"
#include "nodalys/analyses.h"
#include <functional>
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
    /// Why a device could not be stamped at some iterate, such as a behavioral source whose expression has no value
    /// there; empty when none ever failed. It is the likely cause when the solve did not converge.
    std::string Fault;
};

/**
 * @brief   Finds the DC solution of a compiled circuit with Newton-Raphson.
 * @details A solve that finds an operating point and has nodesets runs twice, as SPICE does: first with every nodeset
 *          pulling its node to its voltage, then released, from the point the first pass found. If the pulled pass
 * finds nothing the solve goes on without the nodesets.
 * @param[in,out] sys      The compiled system; devices stamp into it.
 * @param[in,out] devices  The device list of the circuit; holds and limit states.
 * @param[in,out] x        Initial guess in, converged solution out.
 * @param[in]     time     TimeContext indicating DC or transient operating conditions.
 * @param[in]     options  Convergence tolerances and limits.
 * @return  Outcome with iteration count and whether it converged.
 */
NewtonOutcome SolveNewton(MnaSystem &sys,
                          const std::vector<std::unique_ptr<Device>> &devices,
                          std::vector<double> &x,
                          const TimeContext &time,
                          const ConvergenceOptions &options);

/**
 * @enum    NodesetPass
 * @brief   Which of the passes of a solve with nodesets is being run.
 */
enum class NodesetPass {
    /// The nodes are pulled to their guesses.
    PULLED,
    /// The nodesets are let go, starting from the point the pulled pass found.
    RELEASED,
    /// No nodeset acts, as a solve of a circuit without any.
    PLAIN,
};

/**
 * @brief   Runs a solve of an operating point through the passes of the nodesets.
 * @details The one place that knows the order: the pulled pass, then the released one from its point, and a plain
 *          solve from the start when either finds nothing. `SolveNewton` and the DC bias of the harmonic balance both
 *          go through it, so a nodeset means the same in both.
 * @param[in,out] x      Initial guess in, solution out; the guess again when the passes found nothing and the plain
 *                       solve then starts from it.
 * @param[in]     solve  Runs one pass on `x` and returns whether it converged.
 * @return  What the last pass that ran returned.
 */
bool SolveInNodesetPasses(std::vector<double> &x, const std::function<bool(std::vector<double> &, NodesetPass)> &solve);

/**
 * @brief   Adds to the message of a solve that failed the fault of a device, when one was seen.
 * @param[in] message  What the analysis says when the solve does not converge.
 * @param[in] outcome  The outcome of the solve.
 * @return  The message, followed by the fault when there is one.
 */
std::string DescribeFailure(std::string message, const NewtonOutcome &outcome);

} // namespace Nodalys

#endif // NODALYS_NEWTON_H
