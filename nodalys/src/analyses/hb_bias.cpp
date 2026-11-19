/**
 * @file    hb_bias.cpp
 * @brief   The DC bias that a harmonic balance starts from, and the gmin ladder its solves step down.
 */

#include "analyses/hb_bias.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Nodalys {

namespace {

void AddMatrixScaled(MnaSystem &target, const MnaSystem &source) {
    const int n = source.GetSize();
    const DenseMatrix &src_mat = source.Matrix();
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const double v = src_mat.At(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
            if (v != 0.0) {
                target.AddA(r, c, v);
            }
        }
    }
}

/// The first fault among the devices at their last stamp, or an empty text.
std::string_view FindFault(const std::vector<Device *> &devices) {
    for (const Device *device : devices) {
        if (const std::string_view fault = device->GetFault(); !fault.empty()) {
            return fault;
        }
    }
    return {};
}

/// Adds the stiff pull of the nodesets to the DC system while the pulled pass runs, and nothing otherwise.
void StampNodesets(const HbWorkspace &ws, const bool is_nodeset_pass, const std::vector<double> &x, MnaSystem &sys) {
    if (!is_nodeset_pass) {
        return;
    }
    const TimeContext pulled{.IsNodesetPass = true};
    for (Device *nodeset : ws.Nodesets) {
        nodeset->Stamp(sys, x, pulled);
    }
}

/// Stamps the DC system at a state: the linear matrix, the scaled DC excitation, the nonlinear devices and the gmin.
void StampDcSystem(const HbWorkspace &ws,
                   const std::vector<double> &b0,
                   const double b0_scale,
                   const double gmin,
                   const bool is_nodeset_pass,
                   const std::vector<double> &x,
                   MnaSystem &sys) {
    sys.Clear();
    AddMatrixScaled(sys, ws.GLin);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        sys.AddRhs(u, b0_scale * b0[static_cast<std::size_t>(u)]);
    }
    for (Device *device : ws.NonlinearDevices) {
        device->Stamp(sys, x, TimeContext{});
    }
    StampNodesets(ws, is_nodeset_pass, x, sys);
    sys.AddGminToNodes(gmin);
}

/// Starts the guess at zero and lets the nonlinear devices move it to where they conduct.
void SeedDcGuess(const HbWorkspace &ws, std::vector<double> &x) {
    x.assign(static_cast<std::size_t>(ws.UnknownCount), 0.0);
    for (const Device *device : ws.NonlinearDevices) {
        device->SeedOperatingPoint(x);
    }
}

bool RunDcNewtonWithB0(const HbWorkspace &ws,
                       const std::vector<double> &b0,
                       const double b0_scale,
                       const double gmin,
                       const ConvergenceOptions &options,
                       const bool is_nodeset_pass,
                       std::vector<double> &x) {
    const int n = ws.UnknownCount;
    MnaSystem sys(ws.IsNodeUnknown);
    std::vector<double> candidate(static_cast<std::size_t>(n), 0.0);

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        StampDcSystem(ws, b0, b0_scale, gmin, is_nodeset_pass, x, sys);

        if (!sys.Solve(candidate)) {
            return false;
        }

        bool any_limited = false;
        for (const Device *device : ws.NonlinearDevices) {
            if (device->Limit(candidate, x)) {
                any_limited = true;
            }
        }

        bool converged = iteration >= 2 && !any_limited && FindFault(ws.NonlinearDevices).empty();
        for (int index = 0; index < n && converged; ++index) {
            const double abs_tol =
                ws.IsNodeUnknown[static_cast<std::size_t>(index)] ? options.VoltageAbsTol : options.CurrentAbsTol;
            const double tol = options.RelTol * std::fabs(candidate[static_cast<std::size_t>(index)]) + abs_tol;
            if (std::fabs(candidate[static_cast<std::size_t>(index)] - x[static_cast<std::size_t>(index)]) > tol) {
                converged = false;
            }
        }

        x = candidate;

        if (converged) {
            StampDcSystem(ws, b0, b0_scale, gmin, is_nodeset_pass, x, sys);
            if (FindFault(ws.NonlinearDevices).empty()
                && sys.CheckResidual(x, options.VoltageAbsTol, options.CurrentAbsTol, options.RelTol)) {
                return true;
            }
        }
    }
    return false;
}

/// One pass of the DC bias: the direct Newton and its two fallbacks. The released pass of the nodesets starts from the
/// point the pulled one found, so it applies no seed and tries the direct Newton only; the solve then falls back to a
/// plain one.
bool SolveDcBiasPass(const HbWorkspace &ws,
                     const ConvergenceOptions &options,
                     const NodesetPass pass,
                     std::vector<double> &x_dc) {
    const int n = ws.UnknownCount;
    std::vector<double> b0(static_cast<std::size_t>(n), 0.0);
    for (int u = 0; u < n; ++u) {
        b0[static_cast<std::size_t>(u)] = ws.BaseRhsHarmonics[ws.Index(u, 0)];
    }

    const bool is_nodeset_pass = pass == NodesetPass::PULLED;
    if (pass == NodesetPass::RELEASED) {
        return RunDcNewtonWithB0(ws, b0, 1.0, options.Gmin, options, false, x_dc);
    }
    SeedDcGuess(ws, x_dc);
    if (RunDcNewtonWithB0(ws, b0, 1.0, options.Gmin, options, is_nodeset_pass, x_dc)) {
        return true;
    }

    // Fallback 1: Gmin stepping
    SeedDcGuess(ws, x_dc);
    bool gmin_ok = true;
    for (int step = 0; step <= options.GminSteps; ++step) {
        if (!RunDcNewtonWithB0(ws, b0, 1.0, GminAtStep(step, options), options, is_nodeset_pass, x_dc)) {
            gmin_ok = false;
            break;
        }
    }
    if (gmin_ok) {
        return true;
    }

    // Fallback 2: Source stepping on B0
    const int source_steps = std::max(options.SourceSteps, 10);
    SeedDcGuess(ws, x_dc);
    for (int step = 1; step <= source_steps; ++step) {
        const double scale = static_cast<double>(step) / static_cast<double>(source_steps);
        if (!RunDcNewtonWithB0(ws, b0, scale, options.Gmin, options, is_nodeset_pass, x_dc)) {
            return false;
        }
    }
    return true;
}

} // namespace

/**
 * @brief   Gives the gmin of one rung of the ladder that the solves step down from GMIN_START.
 * @param[in] step     Rung of the ladder, zero for the first.
 * @param[in] options  Convergence options, whose Gmin is the floor of the ladder.
 * @return  `GMIN_START * GMIN_FACTOR^step` in siemens, never below the Gmin of the options.
 */
double GminAtStep(const int step, const ConvergenceOptions &options) {
    return std::max(GMIN_START * std::pow(GMIN_FACTOR, static_cast<double>(step)), options.Gmin);
}

/**
 * @brief   Solves the DC bias that the balance starts from, with the DC part of the excitation.
 * @param[in]  ws       Workspace with the linear matrix, the excitation and the nonlinear devices.
 * @param[in]  options  Convergence options.
 * @param[out] x_dc     The DC state, one value per unknown.
 * @return  True when a solve converged.
 */
bool SolveDcBiasForB0(const HbWorkspace &ws, const ConvergenceOptions &options, std::vector<double> &x_dc) {
    if (ws.Nodesets.empty()) {
        return SolveDcBiasPass(ws, options, NodesetPass::PLAIN, x_dc);
    }
    // The nodesets pick the state of the bias that the balance then starts from, as they pick an operating point
    return SolveInNodesetPasses(x_dc, [&](std::vector<double> &guess, const NodesetPass pass) {
        return SolveDcBiasPass(ws, options, pass, guess);
    });
}

} // namespace Nodalys
