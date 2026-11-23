/**
 * @file    hb_residual.cpp
 * @brief   The harmonic balance residual and its Jacobian, by alternating between frequency and time.
 */

#include "analyses/hb/hb_residual.h"

#include "analyses/hb/hb_spectrum.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string_view>

namespace Nodalys {

namespace {

/// Weight of a branch current in the norm of the residual, so amperes do not drown the volts.
constexpr double BRANCH_ROW_WEIGHT = 1e-3;

/**
 * @brief   Adds the linear contribution of one matrix position `(r, col)` to the harmonic residual.
 */
void AddLinearResidualEntry(const HbWorkspace &ws,
                            const std::vector<double> &x_hb,
                            const int r,
                            const int col,
                            const double g_val,
                            const double c_val,
                            HarmonicResidual &residual) {
    if (g_val != 0.0) {
        const double term0 = g_val * x_hb[ws.Index(col, 0)];
        residual.Values[ws.Index(r, 0)] += term0;
        residual.RowScale[ws.Index(r, 0)] = std::max(residual.RowScale[ws.Index(r, 0)], std::abs(term0));
    }

    if (g_val == 0.0 && c_val == 0.0) {
        return;
    }

    for (int k = 1; k <= ws.Harmonics; ++k) {
        const std::size_t rc = ws.Index(r, 2 * k - 1);
        const std::size_t rs = ws.Index(r, 2 * k);
        const std::size_t cc = ws.Index(col, 2 * k - 1);
        const std::size_t cs = ws.Index(col, 2 * k);
        const double kw_c = static_cast<double>(k) * ws.Omega0 * c_val;

        const double xa = x_hb[cc];
        const double xb = x_hb[cs];
        const double y_cos = g_val * xa + kw_c * xb;
        const double y_sin = -kw_c * xa + g_val * xb;
        residual.Values[rc] += y_cos;
        residual.Values[rs] += y_sin;
        residual.RowScale[rc] = std::max(residual.RowScale[rc], std::abs(g_val * xa) + std::abs(kw_c * xb));
        residual.RowScale[rs] = std::max(residual.RowScale[rs], std::abs(kw_c * xa) + std::abs(g_val * xb));
    }
}

/**
 * @brief   Subtracts the excitation B from the residual; the harmonics above DC follow the scale of the ramp of the
 *          sources.
 */
void SubtractExcitation(const HbWorkspace &ws, const double ac_scale, HarmonicResidual &residual) {
    for (int r = 0; r < ws.UnknownCount; ++r) {
        const double b0_val = ws.BaseRhsHarmonics[ws.Index(r, 0)];
        residual.Values[ws.Index(r, 0)] -= b0_val;
        residual.RowScale[ws.Index(r, 0)] = std::max(residual.RowScale[ws.Index(r, 0)], std::abs(b0_val));

        for (int k = 1; k <= ws.Harmonics; ++k) {
            const std::size_t rc = ws.Index(r, 2 * k - 1);
            const std::size_t rs = ws.Index(r, 2 * k);
            const double ba = ac_scale * ws.BaseRhsHarmonics[rc];
            const double bb = ac_scale * ws.BaseRhsHarmonics[rs];
            residual.Values[rc] -= ba;
            residual.Values[rs] -= bb;
            residual.RowScale[rc] = std::max(residual.RowScale[rc], std::abs(ba));
            residual.RowScale[rs] = std::max(residual.RowScale[rs], std::abs(bb));
        }
    }
}

/**
 * @brief   Adds the linear part, `(Y_lin + gmin) * X`, to the residual.
 */
void AddLinearResidual(const HbWorkspace &ws,
                       const std::vector<double> &x_hb,
                       const double gmin,
                       HarmonicResidual &residual) {
    ws.GLin.ForEachNonZero([&](const int r, const int col, const double val) {
        const double g_val = val + ((r == col && ws.IsNodeUnknown[static_cast<std::size_t>(r)]) ? gmin : 0.0);
        AddLinearResidualEntry(ws, x_hb, r, col, g_val, ws.CLin.Entry(r, col), residual);
    });
    if (gmin != 0.0) {
        for (int r = 0; r < ws.UnknownCount; ++r) {
            if (ws.IsNodeUnknown[static_cast<std::size_t>(r)] && ws.GLin.Entry(r, r) == 0.0) {
                AddLinearResidualEntry(ws, x_hb, r, r, gmin, ws.CLin.Entry(r, r), residual);
            }
        }
    }
    ws.CLin.ForEachNonZero([&](const int r, const int col, const double c_val) {
        const bool has_gmin_only = (r == col && ws.IsNodeUnknown[static_cast<std::size_t>(r)] && gmin != 0.0);
        if (ws.GLin.Entry(r, col) == 0.0 && !has_gmin_only) {
            AddLinearResidualEntry(ws, x_hb, r, col, 0.0, c_val, residual);
        }
    });
}

/**
 * @brief   Stamps one conductance entry into the DC and harmonic diagonal blocks of the Jacobian.
 */
void StampLinearConductanceEntry(const HbWorkspace &ws,
                                 const int r,
                                 const int col,
                                 const double g_val,
                                 DenseMatrix &jacobian) {
    jacobian.Add(ws.Index(r, 0), ws.Index(col, 0), g_val);
    for (int k = 1; k <= ws.Harmonics; ++k) {
        jacobian.Add(ws.Index(r, 2 * k - 1), ws.Index(col, 2 * k - 1), g_val);
        jacobian.Add(ws.Index(r, 2 * k), ws.Index(col, 2 * k), g_val);
    }
}

/**
 * @brief   Stamps the blocks of the linear part into the Jacobian; they do not depend on the state.
 */
void StampLinearJacobian(const HbWorkspace &ws, const double gmin, DenseMatrix &jacobian) {
    ws.GLin.ForEachNonZero([&](const int r, const int col, const double g_val) {
        StampLinearConductanceEntry(ws, r, col, g_val, jacobian);
    });
    if (gmin != 0.0) {
        for (int r = 0; r < ws.UnknownCount; ++r) {
            if (ws.IsNodeUnknown[static_cast<std::size_t>(r)]) {
                StampLinearConductanceEntry(ws, r, r, gmin, jacobian);
            }
        }
    }
    ws.CLin.ForEachNonZero([&](const int r, const int col, const double c_val) {
        for (int k = 1; k <= ws.Harmonics; ++k) {
            const double kw_c = static_cast<double>(k) * ws.Omega0 * c_val;
            jacobian.Add(ws.Index(r, 2 * k - 1), ws.Index(col, 2 * k), kw_c);
            jacobian.Add(ws.Index(r, 2 * k), ws.Index(col, 2 * k - 1), -kw_c);
        }
    });
}

/**
 * @brief   Evaluates the nonlinear devices at every time sample of the state, filling the traces of their currents and
 *          charges.
 */
void SampleNonlinearDevices(HbWorkspace &ws, const bool is_recording_matrices) {
    const int n = ws.UnknownCount;
    if (is_recording_matrices) {
        ws.GTraces.Reset(n, ws.SampleCount);
        ws.CTraces.Reset(n, ws.SampleCount);
    }

    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        const std::vector<double> &xm = ws.StateSamples[m];
        ws.SampleG.Clear();
        ws.SampleC.Clear();
        for (Device *device : ws.NonlinearDevices) {
            device->Stamp(ws.SampleG, xm, TimeContext{});
            device->StampCharge(ws.SampleC, xm);
            if (const std::string_view fault = device->GetFault(); !fault.empty()) {
                ws.IsFaulty = true;
                ws.Fault = fault;
            }
        }

        const std::vector<double> &rhsg = ws.SampleG.Rhs();
        const std::vector<double> &rhsc = ws.SampleC.Rhs();
        for (int r = 0; r < n; ++r) {
            ws.FnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] = -rhsg[static_cast<std::size_t>(r)];
            ws.QnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] = -rhsc[static_cast<std::size_t>(r)];
        }
        ws.SampleG.ForEachNonZero([&](const int r, const int c, const double val) {
            ws.FnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] += val * xm[static_cast<std::size_t>(c)];
        });
        ws.SampleC.ForEachNonZero([&](const int r, const int c, const double val) {
            ws.QnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] += val * xm[static_cast<std::size_t>(c)];
        });

        if (is_recording_matrices) {
            ws.GTraces.RecordMatrix(ws.SampleG, m);
            ws.CTraces.RecordMatrix(ws.SampleC, m);
        }
    }
}

/**
 * @brief   Takes the currents and the charges of the nonlinear devices back to the harmonics and adds them to the
 *          residual.
 */
void AddNonlinearResidual(HbWorkspace &ws, HarmonicResidual &residual) {
    ws.TimeSeriesScratch.resize(ws.SampleCount);
    for (int r = 0; r < ws.UnknownCount; ++r) {
        const std::size_t trace_offset = static_cast<std::size_t>(r) * ws.SampleCount;

        // Conductive nonlinear currents
        std::copy_n(ws.FnlTraces.data() + trace_offset, ws.SampleCount, ws.TimeSeriesScratch.data());
        (void)ws.Plan.RealToHarmonics(ws.TimeSeriesScratch, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const double val = ws.CoeffsScratch[static_cast<std::size_t>(slot)];
            residual.Values[ws.Index(r, slot)] += val;
            residual.RowScale[ws.Index(r, slot)] = std::max(residual.RowScale[ws.Index(r, slot)], std::abs(val));
        }

        // Capacitive nonlinear displacement currents: Omega * Gamma(q_nl)
        std::copy_n(ws.QnlTraces.data() + trace_offset, ws.SampleCount, ws.TimeSeriesScratch.data());
        (void)ws.Plan.RealToHarmonics(ws.TimeSeriesScratch, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        for (int k = 1; k <= ws.Harmonics; ++k) {
            const double kw = static_cast<double>(k) * ws.Omega0;
            const double qa = ws.CoeffsScratch[static_cast<std::size_t>(2 * k - 1)];
            const double qb = ws.CoeffsScratch[static_cast<std::size_t>(2 * k)];
            const double ia = kw * qb;
            const double ib = -kw * qa;
            const std::size_t rc = ws.Index(r, 2 * k - 1);
            const std::size_t rs = ws.Index(r, 2 * k);
            residual.Values[rc] += ia;
            residual.Values[rs] += ib;
            residual.RowScale[rc] = std::max(residual.RowScale[rc], std::abs(ia));
            residual.RowScale[rs] = std::max(residual.RowScale[rs], std::abs(ib));
        }
    }
}

/**
 * @brief   Stamps one family of traces, the conductances or the capacitances, as harmonic conversion blocks.
 */
void StampTraces(HbWorkspace &ws, const ActiveMatrixTraces &traces, const bool apply_omega, DenseMatrix &jacobian) {
    const auto c_count = static_cast<std::size_t>(ws.CoeffsPerUnknown);
    for (std::size_t idx = 0; idx < traces.Pairs.size(); ++idx) {
        const auto [r, c] = traces.Pairs[idx];
        std::copy_n(traces.Traces.data() + idx * ws.SampleCount, ws.SampleCount, ws.TimeSeriesScratch.data());
        (void)ws.Plan.RealToNormalizedSpectrum(ws.TimeSeriesScratch, ws.SpectrumScratch);
        StampConversionBlock(jacobian,
                             static_cast<std::size_t>(r) * c_count,
                             static_cast<std::size_t>(c) * c_count,
                             ws.Harmonics,
                             ws.Omega0,
                             ws.SpectrumScratch,
                             apply_omega);
    }
}

/**
 * @brief   Stamps the blocks of the nonlinear devices recorded while they were sampled.
 */
void StampNonlinearJacobian(HbWorkspace &ws, DenseMatrix &jacobian) {
    StampTraces(ws, ws.GTraces, false, jacobian);
    StampTraces(ws, ws.CTraces, true, jacobian);
}

/**
 * @brief   The steps shared by the residual with and without the Jacobian.
 */
void EvaluateSteps(HbWorkspace &ws,
                   const std::vector<double> &x_hb,
                   const double ac_scale,
                   const double gmin,
                   const bool is_recording_matrices,
                   HarmonicResidual &residual) {
    residual.Values.assign(x_hb.size(), 0.0);
    residual.RowScale.assign(x_hb.size(), 0.0);
    ws.IsFaulty = false;

    SubtractExcitation(ws, ac_scale, residual);
    AddLinearResidual(ws, x_hb, gmin, residual);

    // Map the harmonic state to time samples x_m = Gamma^{-1}(X)
    UnpackCoefficientsToSamples(ws, x_hb, ws.StateSamples);
    SampleNonlinearDevices(ws, is_recording_matrices);
    AddNonlinearResidual(ws, residual);
}

} // namespace

/**
 * @brief   Evaluates the residual of the balance at a state, without the Jacobian.
 * @param[in,out] ws        Workspace whose scratch buffers and fault flag are used.
 * @param[in]     x_hb      The harmonics of all unknowns.
 * @param[in]     ac_scale  Scale of the harmonics above DC in the excitation, from zero to one while the sources ramp.
 * @param[in]     gmin      Conductance in siemens held from every node to ground.
 * @param[out]    residual  The residual and the scale of its rows.
 */
void EvaluateResidual(HbWorkspace &ws,
                      const std::vector<double> &x_hb,
                      const double ac_scale,
                      const double gmin,
                      HarmonicResidual &residual) {
    EvaluateSteps(ws, x_hb, ac_scale, gmin, false, residual);
}

/**
 * @brief   Evaluates the residual of the balance at a state, together with its Jacobian.
 * @param[in,out] ws        Workspace whose scratch buffers and fault flag are used.
 * @param[in]     x_hb      The harmonics of all unknowns.
 * @param[in]     ac_scale  Scale of the harmonics above DC in the excitation, from zero to one while the sources ramp.
 * @param[in]     gmin      Conductance in siemens held from every node to ground.
 * @param[out]    residual  The residual and the scale of its rows.
 * @param[out]    jacobian  The derivative of the residual with respect to the state, square and already sized.
 */
void EvaluateResidualAndJacobian(HbWorkspace &ws,
                                 const std::vector<double> &x_hb,
                                 const double ac_scale,
                                 const double gmin,
                                 HarmonicResidual &residual,
                                 DenseMatrix &jacobian) {
    jacobian.Reset();
    StampLinearJacobian(ws, gmin, jacobian);
    EvaluateSteps(ws, x_hb, ac_scale, gmin, true, residual);
    StampNonlinearJacobian(ws, jacobian);
}

/**
 * @brief   Tells whether every row of the residual is within the tolerance of its own scale.
 * @param[in] ws        Workspace that tells the unknowns apart.
 * @param[in] residual  The residual and the scale of its rows.
 * @param[in] options   Tolerances.
 * @return  True when the balance holds.
 */
bool CheckHbResidual(const HbWorkspace &ws, const HarmonicResidual &residual, const ConvergenceOptions &options) {
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const double abstol =
            ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? options.CurrentAbsTol : options.VoltageAbsTol;
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const std::size_t idx = ws.Index(u, slot);
            const double tol = abstol + options.RelTol * residual.RowScale[idx];
            if (std::abs(residual.Values[idx]) > tol) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief   Measures the residual as a weighted sum of squares, for comparing two states.
 * @param[in] ws        Workspace that tells the voltage rows from the branch rows.
 * @param[in] residual  The residual.
 * @return  The sum of squares, with branch rows weighed by BRANCH_ROW_WEIGHT.
 */
double ResidualNormSq(const HbWorkspace &ws, const HarmonicResidual &residual) {
    double sum = 0.0;
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const double weight = ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? 1.0 : BRANCH_ROW_WEIGHT;
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const double r = residual.Values[ws.Index(u, slot)] * weight;
            sum += r * r;
        }
    }
    return sum;
}

} // namespace Nodalys
