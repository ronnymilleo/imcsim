/**
 * @file    newton.cpp
 * @brief   Newton-Raphson driver of Nodalys: the iteration loop, its convergence test, and the homotopy
 *          fallback strategies (`gmin stepping` and `source stepping`).
 * @details
 * ### How the Newton-Raphson Loop Works (`RunNewton`)
 * Each iteration $k = 1 \dots \text{MaxIterations}$ performs 5 steps:
 * 1. **Linearize & Stamp**: Clear $A$ and $\text{rhs}$, stamp every device linearized at $x^{(k-1)}$, and add
 *    $G_{\min}$ to all node diagonals.
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

#include "devices/constraint_devices.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

namespace Nodalys {

namespace {

/// The first fault among the devices at their last stamp, or an empty text.
std::string_view FindFault(const std::vector<std::unique_ptr<Device>> &devices) {
    for (const std::unique_ptr<Device> &device : devices) {
        if (const std::string_view fault = device->GetFault(); !fault.empty()) {
            return fault;
        }
    }
    return {};
}

int RunNewton(MnaSystem &sys,
              const std::vector<std::unique_ptr<Device>> &devices,
              std::vector<double> &x,
              const ConvergenceOptions &options,
              const double gmin,
              const TimeContext &time,
              std::string &fault) {
    const int size = sys.GetSize();
    std::vector<double> candidate(static_cast<std::size_t>(size), 0.0);

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        // Step 1: Linearize and stamp all devices at current iterate `x`
        sys.Clear();
        for (const std::unique_ptr<Device> &device : devices) {
            device->Stamp(sys, x, time);
        }
        sys.AddGminToNodes(gmin);
        // An iterate where a device has no value stamps a placeholder, so it cannot count as converged
        const std::string_view stamp_fault = FindFault(devices);
        if (!stamp_fault.empty()) {
            fault = stamp_fault;
        }

        // Step 2: Solve the linearized MNA system
        if (!sys.Solve(candidate)) {
            return 0;
        }

        // Step 3: Apply PN-junction voltage damping (PNJLIM)
        bool any_limited = false;
        for (const std::unique_ptr<Device> &device : devices) {
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
            sys.Clear();
            for (const std::unique_ptr<Device> &device : devices) {
                device->Stamp(sys, x, time);
            }
            sys.AddGminToNodes(gmin);
            const bool is_faulty = !FindFault(devices).empty();
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

bool HasNodesets(const std::vector<std::unique_ptr<Device>> &devices) {
    return std::ranges::any_of(devices, [](const std::unique_ptr<Device> &device) {
        return dynamic_cast<NodesetDevice *>(device.get()) != nullptr;
    });
}

/**
 * @brief   Gives the devices their starting guesses of an operating point, which a step inside a transient already has.
 */
void SeedOperatingPoint(const std::vector<std::unique_ptr<Device>> &devices,
                        std::vector<double> &x,
                        const TimeContext &time) {
    if (!time.IsTransient || time.IsTransientStart) {
        for (const std::unique_ptr<Device> &device : devices) {
            device->SeedOperatingPoint(x);
        }
    }
}

/**
 * @brief   Gmin stepping: a large conductance (1 mS) from every node to ground makes the network nearly linear, then it
 *          is reduced by 10x per step down to options.Gmin, warm-starting each step from the last.
 */
bool TryGminStepping(MnaSystem &sys,
                     const std::vector<std::unique_ptr<Device>> &devices,
                     std::vector<double> &x,
                     const TimeContext &time,
                     const ConvergenceOptions &options,
                     NewtonOutcome &outcome) {
    constexpr double GMIN_STEP_START = 1e-3;
    constexpr double GMIN_STEP_FACTOR = 0.1;
    constexpr int GMIN_STEPS = 10;
    x.assign(static_cast<std::size_t>(sys.GetSize()), 0.0);
    SeedOperatingPoint(devices, x, time);
    for (int step = 0; step <= GMIN_STEPS; ++step) {
        const double gmin = GMIN_STEP_START * std::pow(GMIN_STEP_FACTOR, static_cast<double>(step));
        if (!RunNewton(sys, devices, x, options, std::max(gmin, options.Gmin), time, outcome.Fault)) {
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
bool TrySourceStepping(MnaSystem &sys,
                       const std::vector<std::unique_ptr<Device>> &devices,
                       std::vector<double> &x,
                       const TimeContext &time,
                       const ConvergenceOptions &options,
                       NewtonOutcome &outcome) {
    constexpr int SOURCE_STEPS = 20;
    x.assign(static_cast<std::size_t>(sys.GetSize()), 0.0);
    SeedOperatingPoint(devices, x, time);
    bool has_converged = true;
    for (int step = 1; step <= SOURCE_STEPS && has_converged; ++step) {
        const double scale = static_cast<double>(step) / static_cast<double>(SOURCE_STEPS);
        for (const std::unique_ptr<Device> &device : devices) {
            device->SetSourceScale(scale);
        }
        has_converged = RunNewton(sys, devices, x, options, options.Gmin, time, outcome.Fault) != 0;
    }
    for (const std::unique_ptr<Device> &device : devices) {
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
 * devices, for the pass that continues from a point already found.
 */
NewtonOutcome SolveWithStrategies(MnaSystem &sys,
                                  const std::vector<std::unique_ptr<Device>> &devices,
                                  std::vector<double> &x,
                                  const TimeContext &time,
                                  const ConvergenceOptions &options,
                                  const bool is_warm_start) {
    if (!is_warm_start) {
        SeedOperatingPoint(devices, x, time);
    }
    NewtonOutcome outcome;
    if (const int iterations = RunNewton(sys, devices, x, options, options.Gmin, time, outcome.Fault)) {
        outcome.Converged = true;
        outcome.Iterations = iterations;
        return outcome;
    }
    if (TryGminStepping(sys, devices, x, time, options, outcome)) {
        return outcome;
    }
    TrySourceStepping(sys, devices, x, time, options, outcome);
    return outcome;
}

} // namespace

NewtonOutcome SolveNewton(MnaSystem &sys,
                          const std::vector<std::unique_ptr<Device>> &devices,
                          std::vector<double> &x,
                          const TimeContext &time,
                          const ConvergenceOptions &options) {
    if (!(time.AppliesNodesets && IsOperatingPointSolve(time) && HasNodesets(devices))) {
        return SolveWithStrategies(sys, devices, x, time, options, false);
    }
    // As SPICE does: find the point with the nodes pulled to their guesses, then let go and converge again from it
    NewtonOutcome outcome;
    SolveInNodesetPasses(x, [&](std::vector<double> &guess, const NodesetPass pass) {
        TimeContext pass_time = time;
        pass_time.IsNodesetPass = pass == NodesetPass::PULLED;
        outcome = SolveWithStrategies(sys, devices, guess, pass_time, options, pass == NodesetPass::RELEASED);
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
