/**
 * @file    newton.cpp
 * @brief   Newton-Raphson driver of Nodalys: the iteration loop, its convergence test, and the homotopy
 *          fallback strategies (`gmin stepping` and `source stepping`).
 * @details
 * ### How the Newton-Raphson Loop Works (`RunNewton`)
 * Each iteration $k = 1 \dots \text{MaxIterations}$ performs 5 steps:
 * 1. **Linearize & Stamp**: Clear $A$ and $\text{rhs}$, stamp every device linearized at $x^{(k-1)}$, apply active
 *    `.ic` / `.nodeset` clamps, and add $G_{\min}$ to all node diagonals.
 * 2. **Solve**: Factorize $A$ with partial-pivoting LU and solve $A \cdot x_{\text{cand}} = \text{rhs}$.
 * 3. **Damp / Limit (`PNJLIM`)**: Call `device->Limit(x_cand, x_prev)` so forward-biased PN junctions cannot jump by
 *    more than a few thermal voltages $V_T$ in a single step.
 * 4. **Step-Delta Convergence Check**: Require that no junction was limited on this step and that every unknown $i$
 *    changed by less than its tolerance. (For node voltages, $0.1 \times \text{RelTol}$ is used on the step delta so
 *    that exponential junction currents $I \propto e^{V/V_T}$ also settle within $\text{RelTol}$.)
 * 5. **KCL Residual Verification**: Re-stamp at the converged candidate $x$ and verify $|A(x)\cdot x - \text{rhs}(x)|$
 *    satisfies KCL at every node and voltage law on every branch.
 */

#include "newton/newton.h"

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

/// The first fault among the devices at their last stamp, or an empty text.
std::string_view FindFault(const std::vector<std::unique_ptr<Device>> &devices) {
    for (const std::unique_ptr<Device> &device : devices) {
        if (const std::string_view fault = device->GetFault(); !fault.empty()) {
            return fault;
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

int RunNewton(CompiledCircuit &compiled,
              std::vector<double> &x,
              const ConvergenceOptions &options,
              const double gmin,
              const TimeContext &time,
              const bool is_nodeset_pass,
              std::string &fault) {
    MnaSystem &sys = *compiled.System;
    const int size = sys.GetSize();
    std::vector<double> candidate(static_cast<std::size_t>(size), 0.0);

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        // Step 1: Linearize and stamp all devices and active node clamps at current iterate `x`
        StampCircuit(compiled, x, time, gmin, is_nodeset_pass);
        // An iterate where a device has no value stamps a placeholder, so it cannot count as converged
        const std::string_view stamp_fault = FindFault(compiled.Devices);
        if (!stamp_fault.empty()) {
            fault = stamp_fault;
        }

        // Step 2: Solve the linearized MNA system
        if (!sys.Solve(candidate)) {
            return 0;
        }

        // Step 3: Apply PN-junction voltage damping (PNJLIM)
        bool any_limited = false;
        for (const std::unique_ptr<Device> &device : compiled.Devices) {
            if (device->Limit(candidate, x)) {
                any_limited = true;
            }
        }

        // Step 4: Check step delta |candidate - x|
        bool converged = iteration >= 2 && !any_limited && stamp_fault.empty();
        for (int index = 0; index < size && converged; ++index) {
            const bool is_node = sys.IsNodeUnknown(index);
            const double abs_tol = is_node ? options.VoltageAbsTol : options.CurrentAbsTol;
            const double tolerance = options.RelTol * std::fabs(candidate[static_cast<std::size_t>(index)]) + abs_tol;
            if (std::fabs(candidate[static_cast<std::size_t>(index)] - x[static_cast<std::size_t>(index)])
                > tolerance) {
                converged = false;
            }
        }

        x = candidate;

        // Step 5: Verify the true equation residual |A(x)*x - rhs(x)| at the un-damped solution `x`
        if (converged) {
            StampCircuit(compiled, x, time, gmin, is_nodeset_pass);
            const bool is_faulty = !FindFault(compiled.Devices).empty();
            if (!is_faulty && sys.CheckResidual(x, options.VoltageAbsTol, options.CurrentAbsTol, options.RelTol)) {
                return iteration;
            }
        }
    }
    return 0;
}

/// Whether the solve finds an operating point, which is what nodesets guide: a DC solve, or the one that starts a
/// transient from its operating point.
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
            device->SeedOperatingPoint(x);
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
            device->SetSourceScale(scale);
        }
        has_converged = RunNewton(compiled, x, options, options.Gmin, time, is_nodeset_pass, outcome.Fault) != 0;
    }
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->SetSourceScale(1.0);
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

void StampNodeClamps(MnaSystem &sys, const std::span<const NodeVoltageDirective> clamps) {
    for (const NodeVoltageDirective &clamp : clamps) {
        StampConductance(sys, clamp.Node, -1, NODE_CLAMP_CONDUCTANCE);
        StampCurrentSource(sys, clamp.Node, -1, -NODE_CLAMP_CONDUCTANCE * clamp.Voltage);
    }
}

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

bool SolveInNodesetPasses(std::vector<double> &x,
                          const std::function<bool(std::vector<double> &, NodesetPass)> &solve) {
    const std::vector<double> start = x;
    if (solve(x, NodesetPass::PULLED) && solve(x, NodesetPass::RELEASED)) {
        return true;
    }
    x = start;
    return solve(x, NodesetPass::PLAIN);
}

std::string DescribeFailure(std::string message, const NewtonOutcome &outcome) {
    if (!outcome.Fault.empty()) {
        message += std::format("; {}", outcome.Fault);
    }
    return message;
}

} // namespace Nodalys
