/**
 * @file    hb_solve.cpp
 * @brief   Solves the harmonic balance: a direct solve for a linear circuit, Newton and its fallbacks for a nonlinear
 * one.
 */

#include "analyses/hb/hb_solve.h"

#include "analyses/hb/hb_bias.h"
#include "analyses/hb/hb_residual.h"
#include "analyses/hb/hb_spectrum.h"
#include "linear/dense_matrix.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

namespace Nodalys {

namespace {

/// A residual that grew by more than this factor in the norm squared makes the step overshoot, so it is shortened.
constexpr double OVERSHOOT_RATIO = 4.0;
/// Shortenings tried before the line search gives up and keeps the whole step.
constexpr int MAX_LINE_SEARCH_STEPS = 4;
/// Factor each shortening of the step multiplies the step by.
constexpr double LINE_SEARCH_SHRINK = 0.5;
/// A solve that took this many iterations or fewer allows the ramp of the sources to take a bigger step.
constexpr int FAST_SOLVE_ITERATIONS = 6;
constexpr double MAX_RAMP_STEP = 0.5;
constexpr double RAMP_GROWTH = 1.5;
constexpr double RAMP_SHRINK = 0.5;
constexpr double MIN_RAMP_STEP = 1e-4;
constexpr double RAMP_END_TOLERANCE = 1e-12;
constexpr int MIN_INITIAL_RAMP_STEPS = 4;

/// How one run of the Newton iteration ended.
struct HbNewtonOutcome {
    bool IsConverged = false;
    int Iterations = 0;
};

/// The vectors and matrices of the Newton iteration, sized once so it allocates nothing per iteration.
struct NewtonBuffers {
    DenseMatrix Jacobian;
    HarmonicResidual Residual;
    std::vector<double> RhsNegative;
    std::vector<double> Delta;
    /// The full Newton step from the state.
    std::vector<double> Candidate;
    /// The state the iteration moves to, after the limiting or the line search.
    std::vector<double> Next;
    std::vector<std::vector<double>> CandidateSamples;
    std::vector<std::vector<double>> PreviousSamples;

    NewtonBuffers(const HbWorkspace &ws, const std::size_t total)
        : Jacobian(total, total), RhsNegative(total), Delta(total), Candidate(total), Next(total),
          CandidateSamples(ws.SampleCount, std::vector<double>(static_cast<std::size_t>(ws.UnknownCount), 0.0)),
          PreviousSamples(ws.SampleCount, std::vector<double>(static_cast<std::size_t>(ws.UnknownCount), 0.0)) {}
};

/**
 * @brief   Solves the Jacobian for the Newton step and takes it from the state, which gives the candidate.
 */
bool TakeNewtonStep(const std::vector<double> &x_hb, NewtonBuffers &buffers) {
    const std::size_t total = x_hb.size();
    for (std::size_t i = 0; i < total; ++i) {
        buffers.RhsNegative[i] = -buffers.Residual.Values[i];
    }
    if (!buffers.Jacobian.FactorizeLU() || !buffers.Jacobian.Solve(buffers.RhsNegative, buffers.Delta)) {
        return false;
    }
    for (std::size_t i = 0; i < total; ++i) {
        buffers.Candidate[i] = x_hb[i] + buffers.Delta[i];
    }
    return true;
}

/**
 * @brief   Applies the per-sample junction and smooth-logic limiting of the devices to the candidate, in the time
 *          domain. When a device limited, the limited samples become the next state.
 */
bool LimitInTimeDomain(HbWorkspace &ws, const std::vector<double> &x_hb, NewtonBuffers &buffers) {
    UnpackCoefficientsToSamples(ws, x_hb, buffers.PreviousSamples);
    UnpackCoefficientsToSamples(ws, buffers.Candidate, buffers.CandidateSamples);
    bool any_limited = false;
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        for (const Device *device : ws.NonlinearDevices) {
            if (device->Limit(buffers.CandidateSamples[m], buffers.PreviousSamples[m])) {
                any_limited = true;
            }
        }
    }
    if (any_limited) {
        PackSamplesToCoefficients(ws, buffers.CandidateSamples, buffers.Next);
    }
    return any_limited;
}

/**
 * @brief   Takes the whole step, unless it overshoots, and then tries shorter ones until one lowers the residual.
 */
void BacktrackLineSearch(HbWorkspace &ws,
                         const std::vector<double> &x_hb,
                         const double prev_norm,
                         const double ac_scale,
                         const double gmin,
                         NewtonBuffers &buffers) {
    const std::size_t total = x_hb.size();
    buffers.Next = buffers.Candidate;
    EvaluateResidual(ws, buffers.Next, ac_scale, gmin, buffers.Residual);
    if (ResidualNormSq(ws, buffers.Residual) > OVERSHOOT_RATIO * prev_norm && prev_norm > 0.0) {
        double alpha = LINE_SEARCH_SHRINK;
        for (int ls = 0; ls < MAX_LINE_SEARCH_STEPS; ++ls) {
            for (std::size_t i = 0; i < total; ++i) {
                buffers.Candidate[i] = x_hb[i] + alpha * buffers.Delta[i];
            }
            EvaluateResidual(ws, buffers.Candidate, ac_scale, gmin, buffers.Residual);
            if (ResidualNormSq(ws, buffers.Residual) < prev_norm) {
                buffers.Next = buffers.Candidate;
                break;
            }
            alpha *= LINE_SEARCH_SHRINK;
        }
    }
}

/**
 * @brief   Tells whether the step moved every harmonic by less than its tolerance.
 */
bool HasStepConverged(const HbWorkspace &ws,
                      const std::vector<double> &x_next,
                      const std::vector<double> &x_hb,
                      const ConvergenceOptions &options) {
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const double abs_tol =
            ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? options.VoltageAbsTol : options.CurrentAbsTol;
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const std::size_t idx = ws.Index(u, slot);
            const double tol = options.RelTol * std::abs(x_next[idx]) + abs_tol;
            if (std::abs(x_next[idx] - x_hb[idx]) > tol) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief   Runs the Newton iteration on the balance from the state in `x_hb`, which ends at the last iterate.
 */
HbNewtonOutcome RunHbNewton(HbWorkspace &ws,
                            const double ac_scale,
                            const double gmin,
                            const ConvergenceOptions &options,
                            std::vector<double> &x_hb) {
    NewtonBuffers buffers(ws, x_hb.size());

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        if (ws.IsStopped || (ws.ShouldStop && ws.ShouldStop())) {
            ws.IsStopped = true;
            return {};
        }
        EvaluateResidualAndJacobian(ws, x_hb, ac_scale, gmin, buffers.Residual, buffers.Jacobian);
        const double prev_norm = ResidualNormSq(ws, buffers.Residual);

        if (!TakeNewtonStep(x_hb, buffers)) {
            return {};
        }
        const bool is_limited = LimitInTimeDomain(ws, x_hb, buffers);
        if (!is_limited) {
            BacktrackLineSearch(ws, x_hb, prev_norm, ac_scale, gmin, buffers);
        }

        const bool is_step_converged =
            iteration >= 2 && !is_limited && HasStepConverged(ws, buffers.Next, x_hb, options);
        x_hb = buffers.Next;

        if (is_step_converged) {
            EvaluateResidual(ws, x_hb, ac_scale, gmin, buffers.Residual);
            if (!ws.IsFaulty && CheckHbResidual(ws, buffers.Residual, options)) {
                return {.IsConverged = true, .Iterations = iteration};
            }
        }
    }
    return {};
}

/**
 * @brief   Starts the harmonics at the DC bias, with nothing at the other harmonics.
 */
void ResetToDc(const HbWorkspace &ws, const std::vector<double> &x_dc, std::vector<double> &x_hb) {
    x_hb.assign(static_cast<std::size_t>(ws.UnknownCount) * static_cast<std::size_t>(ws.CoeffsPerUnknown), 0.0);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        x_hb[ws.Index(u, 0)] = x_dc[static_cast<std::size_t>(u)];
    }
}

/**
 * @brief   Ramps the harmonics of the sources from zero to full with an adaptive step, solving at each stage from the
 *          last.
 */
bool TrySourceStepping(HbWorkspace &ws,
                       const ConvergenceOptions &options,
                       const int initial_steps,
                       const double gmin,
                       std::vector<double> &x_hb,
                       int &total_iterations) {
    double lambda = 0.0;
    double step = 1.0 / static_cast<double>(initial_steps);
    std::vector<double> backup;
    while (lambda < 1.0 - RAMP_END_TOLERANCE && !ws.IsStopped) {
        const double next_lambda = std::min(1.0, lambda + step);
        backup = x_hb;
        const HbNewtonOutcome stage = RunHbNewton(ws, next_lambda, gmin, options, x_hb);
        if (stage.IsConverged) {
            total_iterations += stage.Iterations;
            lambda = next_lambda;
            if (stage.Iterations <= FAST_SOLVE_ITERATIONS) {
                step = std::min(MAX_RAMP_STEP, step * RAMP_GROWTH);
            }
        } else {
            x_hb = std::move(backup);
            step *= RAMP_SHRINK;
            if (step < MIN_RAMP_STEP) {
                return false;
            }
        }
    }
    return !ws.IsStopped;
}

/**
 * @brief   Solves at a large gmin, then lowers it by decades down to the gmin of the options, each solve from the last.
 */
bool TryGminStepping(HbWorkspace &ws,
                     const ConvergenceOptions &options,
                     const int initial_steps,
                     std::vector<double> &x_hb,
                     int &total_iterations) {
    if (!RunHbNewton(ws, 1.0, GMIN_START, options, x_hb).IsConverged
        && !TrySourceStepping(ws, options, initial_steps, GMIN_START, x_hb, total_iterations)) {
        return false;
    }
    for (int step = 1; step <= options.GminSteps; ++step) {
        const HbNewtonOutcome rung = RunHbNewton(ws, 1.0, GminAtStep(step, options), options, x_hb);
        if (!rung.IsConverged) {
            return false;
        }
        total_iterations += rung.Iterations;
    }
    return true;
}

HbSolveOutcome MakeOutcome(const int iterations, std::string strategy) {
    return {.Converged = true, .Iterations = iterations, .Strategy = std::move(strategy)};
}

} // namespace

/**
 * @brief   Solves a circuit with no nonlinear device, one harmonic at a time.
 * @param[in]  ws          Workspace with the linear matrices and the excitation.
 * @param[out] coeffs_out  The coefficients of all unknowns.
 * @return  False when a matrix is singular.
 */
bool SolveLinearCircuit(const HbWorkspace &ws, std::vector<double> &coeffs_out) {
    const int n = ws.UnknownCount;
    coeffs_out.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(ws.CoeffsPerUnknown), 0.0);
    if (n == 0) {
        return true;
    }

    // DC harmonic (k = 0)
    {
        MnaSystem sys_dc = ws.GLin;
        sys_dc.ClearRhs();
        for (int u = 0; u < n; ++u) {
            sys_dc.AddRhs(u, ws.BaseRhsHarmonics[ws.Index(u, 0)]);
        }
        std::vector<double> x0(static_cast<std::size_t>(n), 0.0);
        if (!sys_dc.Solve(x0)) {
            return false;
        }
        for (int u = 0; u < n; ++u) {
            coeffs_out[ws.Index(u, 0)] = x0[static_cast<std::size_t>(u)];
        }
    }

    // Harmonics k = 1 .. H: solve (G_lin + j * k * omega0 * C_lin) * X_k = B_k
    ComplexDenseMatrix mat(static_cast<std::size_t>(n), static_cast<std::size_t>(n));
    std::vector<std::complex<double>> rhs_k(static_cast<std::size_t>(n));
    std::vector<std::complex<double>> sol_k(static_cast<std::size_t>(n));

    for (int k = 1; k <= ws.Harmonics; ++k) {
        const double kw = static_cast<double>(k) * ws.Omega0;
        mat.Reset();
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                const double g_val = ws.GLin.Matrix().At(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
                const double c_val = ws.CLin.Matrix().At(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
                mat.At(static_cast<std::size_t>(r), static_cast<std::size_t>(c)) =
                    std::complex<double>(g_val, kw * c_val);
            }
            const double ba = ws.BaseRhsHarmonics[ws.Index(r, 2 * k - 1)];
            const double bb = ws.BaseRhsHarmonics[ws.Index(r, 2 * k)];
            rhs_k[static_cast<std::size_t>(r)] = std::complex<double>(ba, -bb);
        }
        if (!mat.FactorizeLU() || !mat.Solve(rhs_k, sol_k)) {
            return false;
        }
        for (int u = 0; u < n; ++u) {
            coeffs_out[ws.Index(u, 2 * k - 1)] = sol_k[static_cast<std::size_t>(u)].real();
            coeffs_out[ws.Index(u, 2 * k)] = -sol_k[static_cast<std::size_t>(u)].imag();
        }
    }
    return true;
}

/**
 * @brief   Solves the balance of a circuit with nonlinear devices, from the DC bias, with fallbacks.
 * @details Tries the direct Newton iteration first, then a ramp of the sources, then a ramp of gmin down from a large
 *          conductance.
 * @param[in,out] ws      Workspace; its stop flag is set when the caller asked to stop.
 * @param[in]     config  The source steps the ramp starts with.
 * @param[in]     options  Convergence options.
 * @param[out]    x_hb    The harmonics of all unknowns.
 * @return  Whether a strategy converged, which one, and the iterations it took.
 */
HbSolveOutcome SolveHarmonicBalanceNonlinear(HbWorkspace &ws,
                                             const HarmonicBalanceConfig &config,
                                             const ConvergenceOptions &options,
                                             std::vector<double> &x_hb) {
    std::vector<double> x_dc;
    if (!SolveDcBiasForB0(ws, options, x_dc)) {
        return {};
    }

    // Attempt 1: Direct Harmonic Balance Newton from DC operating point
    ResetToDc(ws, x_dc, x_hb);
    if (const HbNewtonOutcome direct = RunHbNewton(ws, 1.0, options.Gmin, options, x_hb); direct.IsConverged) {
        return MakeOutcome(direct.Iterations, "direct");
    }

    // Attempt 2: Source amplitude stepping (ramp AC harmonics 0 -> 1 with adaptive step size)
    const int initial_steps =
        std::max(config.SourceSteps > 0 ? config.SourceSteps : options.SourceSteps, MIN_INITIAL_RAMP_STEPS);
    ResetToDc(ws, x_dc, x_hb);
    int total_iterations = 0;
    if (TrySourceStepping(ws, options, initial_steps, options.Gmin, x_hb, total_iterations)) {
        return MakeOutcome(total_iterations, "source stepping");
    }

    // Attempt 3: Gmin stepping (start at 1e-3 S with source stepping, then lower gmin by decades)
    ResetToDc(ws, x_dc, x_hb);
    total_iterations = 0;
    if (TryGminStepping(ws, options, initial_steps, x_hb, total_iterations)) {
        return MakeOutcome(total_iterations, "gmin stepping");
    }

    return {};
}

} // namespace Nodalys
