/**
 * @file    newton.cpp
 * @brief   Newton-Raphson driver of Nodalys: the iteration loop, its convergence test, and the homotopy
 *          fallback strategies (`gmin stepping` and `source stepping`).
 * @details
 * ### How the Newton-Raphson Loop Works (`RunNewton`)
 * Each iteration \f$k = 1 \dots \text{MaxIterations}\f$ performs 5 steps:
 * 1. **Linearize & Stamp**: Clear \f$A\f$ and \f$\text{rhs}\f$, stamp every device linearized at \f$x^{(k-1)}\f$, apply
 * active
 *    `.ic` / `.nodeset` clamps, and add \f$G_{\min}\f$ to all node diagonals.
 * 2. **Solve**: Factorize \f$A\f$ with partial-pivoting LU and solve \f$A \cdot x_{\text{cand}} = \text{rhs}\f$.
 * 3. **Damp / Limit (`PNJLIM`)**: Call `device->Limit(x_cand, x_prev)` so forward-biased PN junctions cannot jump by
 *    more than a few thermal voltages \f$V_T\f$ in a single step.
 * 4. **Step-Delta Convergence Check**: Require that no junction was limited on this step and that every unknown \f$i\f$
 *    changed by less than its tolerance. (For node voltages, \f$0.1 \times \text{RelTol}\f$ is used on the step delta
 * so that exponential junction currents \f$I \propto e^{V/V_T}\f$ also settle within \f$\text{RelTol}\f$.)
 * 5. **KCL Residual Verification**: Re-stamp at the converged candidate \f$x\f$ and verify \f$|A(x)\cdot x -
 * \text{rhs}(x)|\f$ satisfies KCL at every node and voltage law on every branch.
 */

#include "newton/newton.h"

#include "devices/sources/independent_sources.h"
#include "devices/stamp_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string_view>

namespace Nodalys {

namespace {

/// Stiff Norton conductance in siemens used to clamp `.ic` and `.nodeset` node voltages to ground.
constexpr double NODE_CLAMP_CONDUCTANCE = 1e9;

/**
 * @brief   The first fault among the devices at their last stamp, or an empty text.
 */
std::string_view FindFault(const std::vector<std::unique_ptr<Device>> &devices) {
    for (const std::unique_ptr<Device> &device : devices) {
        if (const NonlinearDevice *nonlinear = device->AsNonlinear()) {
            if (const std::string_view fault = nonlinear->GetFault(); !fault.empty()) {
                return fault;
            }
        }
    }
    return {};
}

void StampCircuit(CompiledCircuit &compiled,
                  const std::vector<double> &x,
                  const TimeContext &time,
                  const double gmin,
                  const bool is_nodeset_pass) {
    MnaSystem &sys = *compiled.System;
    sys.Clear();
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->Stamp(sys, x, time);
    }
    // With uic there is no operating point to hold the node in; capacitors start from its voltage instead
    if (time.IsTransientStart && !time.UseInitialConditions) {
        StampNodeClamps(sys, compiled.InitialConditions);
    }
    if (is_nodeset_pass) {
        StampNodeClamps(sys, compiled.Nodesets);
    }
    sys.AddGminToNodes(gmin);
}

bool LimitDevices(const std::vector<std::unique_ptr<Device>> &devices,
                  std::vector<double> &candidate,
                  const std::vector<double> &x) {
    bool any_limited = false;
    for (const std::unique_ptr<Device> &device : devices) {
        if (const NonlinearDevice *nonlinear = device->AsNonlinear();
            nonlinear != nullptr && nonlinear->Limit(candidate, x)) {
            any_limited = true;
        }
    }
    return any_limited;
}

bool IsStepDeltaWithinTolerance(const MnaSystem &sys,
                                const std::vector<double> &candidate,
                                const std::vector<double> &x,
                                const ConvergenceOptions &options) {
    const int size = sys.GetSize();
    for (int index = 0; index < size; ++index) {
        const double abs_tol = sys.IsNodeUnknown(index) ? options.VoltageAbsTol : options.CurrentAbsTol;
        const double tolerance = options.RelTol * std::fabs(candidate[static_cast<std::size_t>(index)]) + abs_tol;
        if (std::fabs(candidate[static_cast<std::size_t>(index)] - x[static_cast<std::size_t>(index)]) > tolerance) {
            return false;
        }
    }
    return true;
}

int RunNewton(CompiledCircuit &compiled,
              std::vector<double> &x,
              const ConvergenceOptions &options,
              const double gmin,
              const TimeContext &time,
              const bool is_nodeset_pass,
              std::string &fault) {
    MnaSystem &sys = *compiled.System;
    std::vector<double> candidate(static_cast<std::size_t>(sys.GetSize()), 0.0);

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        StampCircuit(compiled, x, time, gmin, is_nodeset_pass);
        const std::string_view stamp_fault = FindFault(compiled.Devices);
        if (!stamp_fault.empty()) {
            fault = stamp_fault;
        }
        if (!sys.Solve(candidate)) {
            return 0;
        }
        const bool any_limited = LimitDevices(compiled.Devices, candidate, x);
        const bool converged = iteration >= 2 && !any_limited && stamp_fault.empty()
                            && IsStepDeltaWithinTolerance(sys, candidate, x, options);
        x = candidate;

        if (converged) {
            StampCircuit(compiled, x, time, gmin, is_nodeset_pass);
            if (FindFault(compiled.Devices).empty()
                && sys.CheckResidual(x, options.VoltageAbsTol, options.CurrentAbsTol, options.RelTol)) {
                return iteration;
            }
        }
    }
    return 0;
}

/**
 * @brief   Whether the solve finds an operating point, which is what nodesets guide: a DC solve, or the one that starts
 * a transient from its operating point.
 */
bool IsOperatingPointSolve(const TimeContext &time) {
    return !time.IsTransient || (time.IsTransientStart && !time.UseInitialConditions);
}

/**
 * @brief   Gives the devices and bistable seeds their starting guesses of an operating point, which a step inside a
 *          transient already has.
 */
void SeedOperatingPoint(const CompiledCircuit &compiled, std::vector<double> &x, const TimeContext &time) {
    if (!time.IsTransient || time.IsTransientStart) {
        for (const std::unique_ptr<Device> &device : compiled.Devices) {
            if (const NonlinearDevice *nonlinear = device->AsNonlinear()) {
                nonlinear->SeedOperatingPoint(x);
            }
        }
        for (const NodeVoltageDirective &seed : compiled.NodeSeeds) {
            if (seed.Node >= 0 && static_cast<std::size_t>(seed.Node) < x.size()) {
                x[static_cast<std::size_t>(seed.Node)] = seed.Voltage;
            }
        }
    }
}

/**
 * @brief   Gmin stepping: a large conductance (1 mS) from every node to ground makes the network nearly linear, then it
 *          is reduced by 10x per step down to options.Gmin, warm-starting each step from the last.
 */
bool TryGminStepping(CompiledCircuit &compiled,
                     std::vector<double> &x,
                     const TimeContext &time,
                     const bool is_nodeset_pass,
                     const ConvergenceOptions &options,
                     NewtonOutcome &outcome) {
    constexpr double GMIN_STEP_START = 1e-3;
    constexpr double GMIN_STEP_FACTOR = 0.1;
    constexpr int GMIN_STEPS = 10;
    x.assign(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    SeedOperatingPoint(compiled, x, time);
    for (int step = 0; step <= GMIN_STEPS; ++step) {
        const double gmin = GMIN_STEP_START * std::pow(GMIN_STEP_FACTOR, static_cast<double>(step));
        if (!RunNewton(compiled, x, options, std::max(gmin, options.Gmin), time, is_nodeset_pass, outcome.Fault)) {
            return false;
        }
    }
    outcome.Converged = true;
    outcome.Strategy = "gmin stepping";
    return true;
}

/**
 * @brief   Source stepping: all independent sources are scaled from 0 up to 100% in 20 linear steps, tracking the
 *          operating point continuously from the trivial zero solution.
 */
bool TrySourceStepping(CompiledCircuit &compiled,
                       std::vector<double> &x,
                       const TimeContext &time,
                       const bool is_nodeset_pass,
                       const ConvergenceOptions &options,
                       NewtonOutcome &outcome) {
    constexpr int SOURCE_STEPS = 20;
    x.assign(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    SeedOperatingPoint(compiled, x, time);
    bool has_converged = true;
    for (int step = 1; step <= SOURCE_STEPS && has_converged; ++step) {
        const double scale = static_cast<double>(step) / static_cast<double>(SOURCE_STEPS);
        for (const std::unique_ptr<Device> &device : compiled.Devices) {
            if (IndependentSourceDevice *source = device->AsIndependentSource()) {
                source->SetSourceScale(scale);
            }
        }
        has_converged = RunNewton(compiled, x, options, options.Gmin, time, is_nodeset_pass, outcome.Fault) != 0;
    }
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (IndependentSourceDevice *source = device->AsIndependentSource()) {
            source->SetSourceScale(1.0);
        }
    }
    if (has_converged) {
        outcome.Converged = true;
        outcome.Strategy = "source stepping";
    }
    return has_converged;
}

/**
 * @brief   The three strategies that find a solution: plain Newton, gmin stepping and source stepping.
 * @param[in] is_warm_start  Starts the plain attempt from the iterate it is given and not from the seeds of the
 *                           devices, for the pass that continues from a point already found.
 */
NewtonOutcome SolveWithStrategies(CompiledCircuit &compiled,
                                  std::vector<double> &x,
                                  const TimeContext &time,
                                  const bool is_nodeset_pass,
                                  const ConvergenceOptions &options,
                                  const bool is_warm_start) {
    if (!is_warm_start) {
        SeedOperatingPoint(compiled, x, time);
    }
    NewtonOutcome outcome;
    if (const int iterations = RunNewton(compiled, x, options, options.Gmin, time, is_nodeset_pass, outcome.Fault)) {
        outcome.Converged = true;
        outcome.Iterations = iterations;
        return outcome;
    }
    if (TryGminStepping(compiled, x, time, is_nodeset_pass, options, outcome)) {
        return outcome;
    }
    TrySourceStepping(compiled, x, time, is_nodeset_pass, options, outcome);
    return outcome;
}

} // namespace

/**
 * @brief   Stamps stiff Norton voltage clamps to ground for `.ic` or `.nodeset` node directives.
 * @param[in,out] sys     System to stamp into.
 * @param[in]     clamps  Node unknown indices and target voltages to clamp.
 */
void StampNodeClamps(MnaSystem &sys, const std::span<const NodeVoltageDirective> clamps) {
    for (const NodeVoltageDirective &clamp : clamps) {
        StampConductance(sys, clamp.Node, -1, NODE_CLAMP_CONDUCTANCE);
        StampCurrentSource(sys, clamp.Node, -1, -NODE_CLAMP_CONDUCTANCE * clamp.Voltage);
    }
}

/**
 * @brief   Finds the DC or transient-step solution of a compiled circuit with Newton-Raphson.
 * @details A solve that finds an operating point and has nodesets runs twice, as SPICE does: first with every nodeset
 *          pulling its node to its voltage, then released, from the point the first pass found. If the pulled pass
 *          finds nothing the solve goes on without the nodesets.
 * @param[in,out] compiled  The compiled circuit holding the MNA system, devices, and node directives.
 * @param[in,out] x         Initial guess in, converged solution out.
 * @param[in]     time      TimeContext indicating DC or transient operating conditions.
 * @param[in]     options   Convergence tolerances and limits.
 * @return  Outcome with iteration count, fallback strategy, fault reason, and whether it converged.
 */
NewtonOutcome SolveNewton(CompiledCircuit &compiled,
                          std::vector<double> &x,
                          const TimeContext &time,
                          const ConvergenceOptions &options) {
    if (!(time.AppliesNodesets && IsOperatingPointSolve(time) && !compiled.Nodesets.empty())) {
        return SolveWithStrategies(compiled, x, time, false, options, false);
    }
    // As SPICE does: find the point with the nodes pulled to their guesses, then let go and converge again from it
    NewtonOutcome outcome;
    SolveInNodesetPasses(x, [&](std::vector<double> &guess, const NodesetPass pass) {
        outcome = SolveWithStrategies(compiled,
                                      guess,
                                      time,
                                      pass == NodesetPass::PULLED,
                                      options,
                                      pass == NodesetPass::RELEASED);
        return outcome.Converged;
    });
    return outcome;
}

/**
 * @brief   Runs a solve of an operating point through the passes of the nodesets.
 * @details The one place that knows the order: the pulled pass, then the released one from its point, and a plain
 *          solve from the start when either finds nothing. `SolveNewton` and the DC bias of the harmonic balance both
 *          go through it, so a nodeset means the same in both.
 * @param[in,out] x      Initial guess in, solution out; restored to the initial guess when the passes fail before the
 *                       plain solve runs.
 * @param[in]     solve  Runs one pass on `x` and returns whether it converged.
 * @return  What the last pass that ran returned.
 */
bool SolveInNodesetPasses(std::vector<double> &x,
                          const std::function<bool(std::vector<double> &, NodesetPass)> &solve) {
    const std::vector<double> start = x;
    if (solve(x, NodesetPass::PULLED) && solve(x, NodesetPass::RELEASED)) {
        return true;
    }
    x = start;
    return solve(x, NodesetPass::PLAIN);
}

/**
 * @brief   Adds to the message of a solve that failed the fault of a device, when one was seen.
 * @param[in] message  What the analysis says when the solve does not converge.
 * @param[in] outcome  The outcome of the solve.
 * @return  The message, followed by the fault when there is one.
 */
std::string DescribeFailure(std::string message, const NewtonOutcome &outcome) {
    if (!outcome.Fault.empty()) {
        message += std::format("; {}", outcome.Fault);
    }
    return message;
}

} // namespace Nodalys
