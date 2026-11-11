/**
 * @file    hb_analysis.cpp
 * @brief   Periodic steady-state Harmonic Balance (.hb) solver via Alternating Frequency-Time (AFT) Newton-Raphson.
 * @details
 * ### Mathematical Formulation
 * Each MNA unknown $x_u(t)$ is expanded in a truncated real Fourier series up to harmonic $H$:
 * $$x_u(t) = a_{u,0} + \sum_{k=1}^{H} \left( a_{u,k} \cos(k \omega_0 t) + b_{u,k} \sin(k \omega_0 t) \right)$$
 * which corresponds to peak complex phasors $X_{u,0} = a_{u,0}$ and $X_{u,k} = a_{u,k} - j b_{u,k}$ for $k \ge 1$.
 *
 * Separating linear devices ($\mathbf{G}_{\text{lin}}, \mathbf{C}_{\text{lin}}$) from nonlinear devices
 * ($\mathbf{f}_{\text{nl}},
 * \mathbf{q}_{\text{nl}}$) yields the real harmonic residual:
 * $$\mathbf{F}(\mathbf{X}) = \mathbf{Y}_{\text{lin}} \mathbf{X} + \Gamma \,
 * \mathbf{f}_{\text{nl}}(\Gamma^{-1}\mathbf{X})
 *   + \Omega \, \Gamma \, \mathbf{q}_{\text{nl}}(\Gamma^{-1}\mathbf{X}) - \mathbf{B} = \mathbf{0}$$
 * where $\mathbf{Y}_{\text{lin}}$ has $2\times 2$ blocks $\begin{bmatrix} \mathbf{G}_{\text{lin}} & k\omega_0
 * \mathbf{C}_{\text{lin}} \\ -k\omega_0 \mathbf{C}_{\text{lin}} & \mathbf{G}_{\text{lin}} \end{bmatrix}$ for $k \ge 1$,
 * $\Gamma$ / $\Gamma^{-1}$ are the real-complex radix-2 FFT operators over $M$ samples, and $\Omega$ applies
 * $\frac{d}{dt}$ in the cosine/sine basis.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include "linear/dense_matrix.h"
#include "linear/fft.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nodalys {

namespace {

constexpr std::size_t MIN_AUTO_SAMPLES = 16;
constexpr std::size_t MAX_DENSE_UNKNOWNS_WARNING = 4000;
constexpr double FREQ_RATIO_RELTOL = 1e-9;

struct PartitionedCircuit {
    std::vector<Device *> LinearDevices;
    std::vector<Device *> NonlinearDevices;
    std::vector<const VoltageSourceDevice *> VoltageSources;
    std::vector<const CurrentSourceDevice *> CurrentSources;
};

struct ActiveMatrixTraces {
    std::vector<std::pair<int, int>> Pairs;
    std::vector<int> SlotByFlatIndex;
    std::vector<double> Traces; // Flat: [pair_idx * sample_count + m]

    void Reset(const int unknown_count, const std::size_t sample_count) {
        Pairs.clear();
        SlotByFlatIndex.assign(static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(unknown_count), -1);
        Traces.clear();
        (void)sample_count;
    }

    void RecordMatrix(const DenseMatrix &mat, const int unknown_count, const std::size_t sample_idx,
                      const std::size_t sample_count) {
        for (int r = 0; r < unknown_count; ++r) {
            for (int c = 0; c < unknown_count; ++c) {
                const double val = mat.At(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
                if (val == 0.0) {
                    continue;
                }
                const std::size_t flat =
                    static_cast<std::size_t>(r) * static_cast<std::size_t>(unknown_count) + static_cast<std::size_t>(c);
                int slot = SlotByFlatIndex[flat];
                if (slot < 0) {
                    slot = static_cast<int>(Pairs.size());
                    SlotByFlatIndex[flat] = slot;
                    Pairs.emplace_back(r, c);
                    Traces.resize(Pairs.size() * sample_count, 0.0);
                }
                Traces[static_cast<std::size_t>(slot) * sample_count + sample_idx] = val;
            }
        }
    }
};

struct HbWorkspace {
    int UnknownCount = 0;
    int Harmonics = 0;
    int CoeffsPerUnknown = 0;
    std::size_t SampleCount = 0;
    double Omega0 = 0.0;
    FftPlan Plan;
    std::vector<bool> IsNodeUnknown;

    MnaSystem GLin;
    MnaSystem CLin;
    std::vector<Device *> NonlinearDevices;
    std::vector<double> BaseRhsHarmonics; // B in [u * C + c]

    // Scratch buffers reused across Newton iterations
    std::vector<std::complex<double>> FftScratch;
    std::vector<double> CoeffsScratch;
    std::vector<double> TimeSeriesScratch;
    std::vector<std::complex<double>> SpectrumScratch;
    std::vector<std::vector<double>> StateSamples; // [m][u]
    std::vector<double> FnlTraces;                 // [u * M + m]
    std::vector<double> QnlTraces;                 // [u * M + m]
    ActiveMatrixTraces GTraces;
    ActiveMatrixTraces CTraces;
    MnaSystem SampleG;
    MnaSystem SampleC;

    HbWorkspace(const int unknown_count, const int harmonics, const std::size_t sample_count, const double f0,
                std::vector<bool> is_node_unknown)
        : UnknownCount(unknown_count), Harmonics(harmonics), CoeffsPerUnknown(2 * harmonics + 1),
          SampleCount(sample_count), Omega0(2.0 * std::numbers::pi * f0), Plan(sample_count),
          IsNodeUnknown(is_node_unknown), GLin(is_node_unknown), CLin(is_node_unknown),
          BaseRhsHarmonics(static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(2 * harmonics + 1), 0.0),
          StateSamples(sample_count, std::vector<double>(static_cast<std::size_t>(unknown_count), 0.0)),
          FnlTraces(static_cast<std::size_t>(unknown_count) * sample_count, 0.0),
          QnlTraces(static_cast<std::size_t>(unknown_count) * sample_count, 0.0), SampleG(is_node_unknown),
          SampleC(std::move(is_node_unknown)) {}

    std::size_t Index(const int unknown, const int coeff_slot) const {
        return static_cast<std::size_t>(unknown) * static_cast<std::size_t>(CoeffsPerUnknown) +
               static_cast<std::size_t>(coeff_slot);
    }
};

std::expected<std::size_t, std::string> ValidateAndResolveSampleCount(const HarmonicBalanceConfig &config) {
    if (!std::isfinite(config.FundamentalFrequency) || config.FundamentalFrequency <= 0.0) {
        return std::unexpected("Harmonic Balance requires a positive finite fundamental frequency");
    }
    if (config.Harmonics < 0) {
        return std::unexpected("Harmonic Balance requires a non-negative number of harmonics");
    }
    if (config.Samples == 0) {
        const std::size_t min_samples =
            std::max(MIN_AUTO_SAMPLES, static_cast<std::size_t>(4) * static_cast<std::size_t>(config.Harmonics));
        return NextPowerOfTwo(min_samples);
    }
    if (config.Samples < 0 || !IsPowerOfTwo(static_cast<std::size_t>(config.Samples))) {
        return std::unexpected("Harmonic Balance sample count must be a positive power of two");
    }
    const auto sample_count = static_cast<std::size_t>(config.Samples);
    if (sample_count <= static_cast<std::size_t>(2) * static_cast<std::size_t>(config.Harmonics)) {
        return std::unexpected(std::format(
            "Harmonic Balance sample count ({}) must exceed twice the harmonic count ({}) to prevent aliasing",
            sample_count, config.Harmonics));
    }
    return sample_count;
}

std::expected<PartitionedCircuit, std::string> ValidateAndPartitionDevices(const CompiledCircuit &compiled,
                                                                           const HarmonicBalanceConfig &config,
                                                                           const std::size_t sample_count,
                                                                           std::vector<std::string> &warnings) {
    PartitionedCircuit partitioned;
    const double period = 1.0 / config.FundamentalFrequency;
    const double sample_dt = period / static_cast<double>(sample_count);
    bool warned_sharp_edge = false;

    const auto check_periodicity = [&](const SourcePeriodicity &periodicity,
                                       std::string_view kind_label) -> std::expected<void, std::string> {
        if (!periodicity.IsSupportedInHb) {
            return std::unexpected(
                std::format("Harmonic Balance does not support non-periodic or damped {} sources", kind_label));
        }
        if (periodicity.Kind == SourceWaveformKind::Dc) {
            return {};
        }
        if (periodicity.Frequency <= 0.0) {
            return {};
        }
        const double ratio = periodicity.Frequency / config.FundamentalFrequency;
        const double harmonic_nearest = std::round(ratio);
        if (harmonic_nearest < 1.0 || std::abs(ratio - harmonic_nearest) > FREQ_RATIO_RELTOL * harmonic_nearest) {
            return std::unexpected(std::format("Source frequency {} Hz is not an integer multiple of fundamental {} Hz "
                                               "for single-tone Harmonic Balance",
                                               periodicity.Frequency, config.FundamentalFrequency));
        }
        if (static_cast<int>(harmonic_nearest) > config.Harmonics) {
            return std::unexpected(std::format("Source frequency {} Hz (harmonic {}) exceeds configured Harmonics = {}",
                                               periodicity.Frequency, static_cast<int>(harmonic_nearest),
                                               config.Harmonics));
        }
        if ((periodicity.Kind == SourceWaveformKind::Pulse || periodicity.Kind == SourceWaveformKind::Clock) &&
            !warned_sharp_edge && periodicity.MinEdgeTime < sample_dt) {
            warnings.push_back(std::format(
                "A periodic pulse/clock edge ({:.3g} s) is faster than the time grid ({:.3g} s); increase Samples or "
                "Harmonics to reduce Gibbs/aliasing distortion",
                periodicity.MinEdgeTime, sample_dt));
            warned_sharp_edge = true;
        }
        return {};
    };

    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (const auto *vs = dynamic_cast<const VoltageSourceDevice *>(device.get())) {
            if (auto ok = check_periodicity(vs->GetPeriodicity(), "voltage"); !ok) {
                return std::unexpected(ok.error());
            }
            partitioned.VoltageSources.push_back(vs);
        } else if (const auto *cs = dynamic_cast<const CurrentSourceDevice *>(device.get())) {
            if (auto ok = check_periodicity(cs->GetPeriodicity(), "current"); !ok) {
                return std::unexpected(ok.error());
            }
            partitioned.CurrentSources.push_back(cs);
        } else if (!device->SupportsHarmonicBalance()) {
            return std::unexpected("Harmonic Balance does not support hysteretic switches or relays");
        }

        if (device->IsLinear()) {
            partitioned.LinearDevices.push_back(device.get());
        } else {
            partitioned.NonlinearDevices.push_back(device.get());
        }
    }
    return partitioned;
}

void AssembleLinearAndExcitation(HbWorkspace &ws, const PartitionedCircuit &partitioned, const double period) {
    const std::vector<double> zero_x(static_cast<std::size_t>(ws.UnknownCount), 0.0);
    ws.GLin.Clear();
    ws.CLin.Clear();
    for (Device *device : partitioned.LinearDevices) {
        device->Stamp(ws.GLin, zero_x, TimeContext{});
        device->StampAc(ws.CLin, zero_x);
    }

    // Sample the independent sources' RHS over one steady-state period [0, T0) and transform to B
    MnaSystem rhs_probe(ws.IsNodeUnknown);
    std::vector<double> rhs_samples(static_cast<std::size_t>(ws.UnknownCount) * ws.SampleCount, 0.0);
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        rhs_probe.Clear();
        const double t_m = static_cast<double>(m) * period / static_cast<double>(ws.SampleCount);
        const TimeContext steady_state{.IsPeriodicSteadyState = true, .Time = t_m};
        for (const VoltageSourceDevice *vs : partitioned.VoltageSources) {
            const_cast<VoltageSourceDevice *>(vs)->Stamp(rhs_probe, zero_x, steady_state);
        }
        for (const CurrentSourceDevice *cs : partitioned.CurrentSources) {
            const_cast<CurrentSourceDevice *>(cs)->Stamp(rhs_probe, zero_x, steady_state);
        }
        const std::vector<double> &rhs = rhs_probe.Rhs();
        for (int u = 0; u < ws.UnknownCount; ++u) {
            rhs_samples[static_cast<std::size_t>(u) * ws.SampleCount + m] = rhs[static_cast<std::size_t>(u)];
        }
    }

    std::vector<double> series(ws.SampleCount);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const std::size_t base = static_cast<std::size_t>(u) * ws.SampleCount;
        std::copy_n(rhs_samples.data() + base, ws.SampleCount, series.data());
        (void)ws.Plan.RealToHarmonics(series, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        for (int c = 0; c < ws.CoeffsPerUnknown; ++c) {
            ws.BaseRhsHarmonics[ws.Index(u, c)] = ws.CoeffsScratch[static_cast<std::size_t>(c)];
        }
    }
}

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

void AddMatrixScaled(MnaSystem &target, const MnaSystem &source) {
    const int n = source.Size();
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

bool RunDcNewtonWithB0(const HbWorkspace &ws, const std::vector<double> &b0, const double b0_scale, const double gmin,
                       const ConvergenceOptions &options, std::vector<double> &x) {
    const int n = ws.UnknownCount;
    MnaSystem sys(ws.IsNodeUnknown);
    std::vector<double> candidate(static_cast<std::size_t>(n), 0.0);

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        sys.Clear();
        AddMatrixScaled(sys, ws.GLin);
        for (int u = 0; u < n; ++u) {
            sys.AddRhs(u, b0_scale * b0[static_cast<std::size_t>(u)]);
        }
        for (Device *device : ws.NonlinearDevices) {
            device->Stamp(sys, x, TimeContext{});
        }
        sys.AddGminToNodes(gmin);

        if (!sys.Solve(candidate)) {
            return false;
        }

        bool any_limited = false;
        for (const Device *device : ws.NonlinearDevices) {
            if (device->Limit(candidate, x)) {
                any_limited = true;
            }
        }

        bool converged = iteration >= 2 && !any_limited;
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
            sys.Clear();
            AddMatrixScaled(sys, ws.GLin);
            for (int u = 0; u < n; ++u) {
                sys.AddRhs(u, b0_scale * b0[static_cast<std::size_t>(u)]);
            }
            for (Device *device : ws.NonlinearDevices) {
                device->Stamp(sys, x, TimeContext{});
            }
            sys.AddGminToNodes(gmin);
            if (sys.CheckResidual(x, options.VoltageAbsTol, options.CurrentAbsTol, options.RelTol)) {
                return true;
            }
        }
    }
    return false;
}

bool SolveDcBiasForB0(const HbWorkspace &ws, const ConvergenceOptions &options, std::vector<double> &x_dc) {
    const int n = ws.UnknownCount;
    std::vector<double> b0(static_cast<std::size_t>(n), 0.0);
    for (int u = 0; u < n; ++u) {
        b0[static_cast<std::size_t>(u)] = ws.BaseRhsHarmonics[ws.Index(u, 0)];
    }

    x_dc.assign(static_cast<std::size_t>(n), 0.0);
    for (const Device *device : ws.NonlinearDevices) {
        device->SeedOperatingPoint(x_dc);
    }
    if (RunDcNewtonWithB0(ws, b0, 1.0, options.Gmin, options, x_dc)) {
        return true;
    }

    // Fallback 1: Gmin stepping
    constexpr double GMIN_START = 1e-3;
    constexpr double GMIN_FACTOR = 0.1;
    x_dc.assign(static_cast<std::size_t>(n), 0.0);
    for (const Device *device : ws.NonlinearDevices) {
        device->SeedOperatingPoint(x_dc);
    }
    bool gmin_ok = true;
    for (int step = 0; step <= options.GminSteps; ++step) {
        const double gmin = std::max(GMIN_START * std::pow(GMIN_FACTOR, static_cast<double>(step)), options.Gmin);
        if (!RunDcNewtonWithB0(ws, b0, 1.0, gmin, options, x_dc)) {
            gmin_ok = false;
            break;
        }
    }
    if (gmin_ok) {
        return true;
    }

    // Fallback 2: Source stepping on B0
    const int source_steps = std::max(options.SourceSteps, 10);
    x_dc.assign(static_cast<std::size_t>(n), 0.0);
    for (const Device *device : ws.NonlinearDevices) {
        device->SeedOperatingPoint(x_dc);
    }
    for (int step = 1; step <= source_steps; ++step) {
        const double scale = static_cast<double>(step) / static_cast<double>(source_steps);
        if (!RunDcNewtonWithB0(ws, b0, scale, options.Gmin, options, x_dc)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief   Stamps a (2H+1) x (2H+1) harmonic conversion matrix block into the real Jacobian from a
 *          1/M-normalized time-domain entry spectrum $\hat{g}_n$.
 * @details When `apply_omega` is true, pre-multiplies by the time-derivative operator $\Omega$ for nonlinear charges.
 */
void StampConversionBlock(DenseMatrix &jacobian, const std::size_t row_offset, const std::size_t col_offset,
                          const int harmonics, const double omega0, const std::vector<std::complex<double>> &spectrum,
                          const bool apply_omega) {
    if (!apply_omega) {
        jacobian.Add(row_offset, col_offset, spectrum[0].real());
        for (int l = 1; l <= harmonics; ++l) {
            const std::size_t cos_col = col_offset + static_cast<std::size_t>(2 * l - 1);
            const std::size_t sin_col = col_offset + static_cast<std::size_t>(2 * l);
            const std::complex<double> &gl = spectrum[static_cast<std::size_t>(l)];
            jacobian.Add(row_offset, cos_col, gl.real());
            jacobian.Add(row_offset, sin_col, -gl.imag());
        }
    }

    for (int k = 1; k <= harmonics; ++k) {
        const std::size_t cos_row = row_offset + static_cast<std::size_t>(2 * k - 1);
        const std::size_t sin_row = row_offset + static_cast<std::size_t>(2 * k);
        const double kw = static_cast<double>(k) * omega0;

        // Column l = 0 (DC)
        const std::complex<double> &gk = spectrum[static_cast<std::size_t>(k)];
        if (!apply_omega) {
            jacobian.Add(cos_row, col_offset, 2.0 * gk.real());
            jacobian.Add(sin_row, col_offset, -2.0 * gk.imag());
        } else {
            jacobian.Add(cos_row, col_offset, kw * (-2.0 * gk.imag()));
            jacobian.Add(sin_row, col_offset, -kw * (2.0 * gk.real()));
        }

        // Columns l = 1 .. H
        for (int l = 1; l <= harmonics; ++l) {
            const std::size_t cos_col = col_offset + static_cast<std::size_t>(2 * l - 1);
            const std::size_t sin_col = col_offset + static_cast<std::size_t>(2 * l);
            const std::complex<double> g_minus = (k >= l) ? spectrum[static_cast<std::size_t>(k - l)]
                                                          : std::conj(spectrum[static_cast<std::size_t>(l - k)]);
            const std::complex<double> &g_plus = spectrum[static_cast<std::size_t>(k + l)];

            const double a_minus = g_minus.real();
            const double b_minus = g_minus.imag();
            const double a_plus = g_plus.real();
            const double b_plus = g_plus.imag();

            const double cc = a_minus + a_plus;
            const double cs = b_minus - b_plus;
            const double sc = -(b_minus + b_plus);
            const double ss = a_minus - a_plus;

            if (!apply_omega) {
                jacobian.Add(cos_row, cos_col, cc);
                jacobian.Add(cos_row, sin_col, cs);
                jacobian.Add(sin_row, cos_col, sc);
                jacobian.Add(sin_row, sin_col, ss);
            } else {
                jacobian.Add(cos_row, cos_col, kw * sc);
                jacobian.Add(cos_row, sin_col, kw * ss);
                jacobian.Add(sin_row, cos_col, -kw * cc);
                jacobian.Add(sin_row, sin_col, -kw * cs);
            }
        }
    }
}

void UnpackCoefficientsToSamples(HbWorkspace &ws, const std::vector<double> &coeffs,
                                 std::vector<std::vector<double>> &samples) {
    ws.CoeffsScratch.resize(static_cast<std::size_t>(ws.CoeffsPerUnknown));
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const std::size_t base = static_cast<std::size_t>(u) * static_cast<std::size_t>(ws.CoeffsPerUnknown);
        std::copy_n(coeffs.data() + base, static_cast<std::size_t>(ws.CoeffsPerUnknown), ws.CoeffsScratch.data());
        (void)ws.Plan.HarmonicsToReal(ws.CoeffsScratch, ws.TimeSeriesScratch, ws.FftScratch);
        for (std::size_t m = 0; m < ws.SampleCount; ++m) {
            samples[m][static_cast<std::size_t>(u)] = ws.TimeSeriesScratch[m];
        }
    }
}

void PackSamplesToCoefficients(HbWorkspace &ws, const std::vector<std::vector<double>> &samples,
                               std::vector<double> &coeffs) {
    coeffs.resize(static_cast<std::size_t>(ws.UnknownCount) * static_cast<std::size_t>(ws.CoeffsPerUnknown));
    ws.TimeSeriesScratch.resize(ws.SampleCount);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        for (std::size_t m = 0; m < ws.SampleCount; ++m) {
            ws.TimeSeriesScratch[m] = samples[m][static_cast<std::size_t>(u)];
        }
        (void)ws.Plan.RealToHarmonics(ws.TimeSeriesScratch, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        const std::size_t base = static_cast<std::size_t>(u) * static_cast<std::size_t>(ws.CoeffsPerUnknown);
        std::copy_n(ws.CoeffsScratch.data(), static_cast<std::size_t>(ws.CoeffsPerUnknown), coeffs.data() + base);
    }
}

void EvaluateResidualAndJacobian(HbWorkspace &ws, const std::vector<double> &x_hb, const double ac_scale,
                                 const double gmin, const bool build_jacobian, std::vector<double> &residual,
                                 std::vector<double> &row_scale, DenseMatrix *jacobian) {
    const int n = ws.UnknownCount;
    const int c_count = ws.CoeffsPerUnknown;
    const std::size_t total = x_hb.size();
    residual.assign(total, 0.0);
    row_scale.assign(total, 0.0);

    const DenseMatrix &g_lin = ws.GLin.Matrix();
    const DenseMatrix &c_lin = ws.CLin.Matrix();

    // 1. Linear residual (Y_lin + gmin_nodes) * X - B_scaled, and linear Jacobian blocks
    if (build_jacobian && jacobian) {
        jacobian->Reset();
    }

    for (int r = 0; r < n; ++r) {
        const double b0_val = ws.BaseRhsHarmonics[ws.Index(r, 0)];
        residual[ws.Index(r, 0)] -= b0_val;
        row_scale[ws.Index(r, 0)] = std::max(row_scale[ws.Index(r, 0)], std::abs(b0_val));

        for (int k = 1; k <= ws.Harmonics; ++k) {
            const std::size_t rc = ws.Index(r, 2 * k - 1);
            const std::size_t rs = ws.Index(r, 2 * k);
            const double ba = ac_scale * ws.BaseRhsHarmonics[rc];
            const double bb = ac_scale * ws.BaseRhsHarmonics[rs];
            residual[rc] -= ba;
            residual[rs] -= bb;
            row_scale[rc] = std::max(row_scale[rc], std::abs(ba));
            row_scale[rs] = std::max(row_scale[rs], std::abs(bb));
        }

        for (int col = 0; col < n; ++col) {
            const double g_val = g_lin.At(static_cast<std::size_t>(r), static_cast<std::size_t>(col)) +
                                 ((r == col && ws.IsNodeUnknown[static_cast<std::size_t>(r)]) ? gmin : 0.0);
            const double c_val = c_lin.At(static_cast<std::size_t>(r), static_cast<std::size_t>(col));

            if (g_val != 0.0) {
                const double term0 = g_val * x_hb[ws.Index(col, 0)];
                residual[ws.Index(r, 0)] += term0;
                row_scale[ws.Index(r, 0)] = std::max(row_scale[ws.Index(r, 0)], std::abs(term0));
                if (build_jacobian && jacobian) {
                    jacobian->Add(ws.Index(r, 0), ws.Index(col, 0), g_val);
                }
            }

            if (g_val != 0.0 || c_val != 0.0) {
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
                    residual[rc] += y_cos;
                    residual[rs] += y_sin;
                    row_scale[rc] = std::max(row_scale[rc], std::abs(g_val * xa) + std::abs(kw_c * xb));
                    row_scale[rs] = std::max(row_scale[rs], std::abs(kw_c * xa) + std::abs(g_val * xb));

                    if (build_jacobian && jacobian) {
                        jacobian->Add(rc, cc, g_val);
                        jacobian->Add(rc, cs, kw_c);
                        jacobian->Add(rs, cc, -kw_c);
                        jacobian->Add(rs, cs, g_val);
                    }
                }
            }
        }
    }

    // 2. Map current harmonic state to time samples x_m = Gamma^{-1}(X)
    UnpackCoefficientsToSamples(ws, x_hb, ws.StateSamples);

    // 3. Evaluate nonlinear currents f_nl(x_m), charges q_nl(x_m), and differential matrices G_nl(x_m), C_nl(x_m)
    if (build_jacobian) {
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
        }

        const DenseMatrix &gm = ws.SampleG.Matrix();
        const DenseMatrix &cm = ws.SampleC.Matrix();
        const std::vector<double> &rhsg = ws.SampleG.Rhs();
        const std::vector<double> &rhsc = ws.SampleC.Rhs();

        for (int r = 0; r < n; ++r) {
            double fnl_r = -rhsg[static_cast<std::size_t>(r)];
            double qnl_r = -rhsc[static_cast<std::size_t>(r)];
            for (int c = 0; c < n; ++c) {
                fnl_r +=
                    gm.At(static_cast<std::size_t>(r), static_cast<std::size_t>(c)) * xm[static_cast<std::size_t>(c)];
                qnl_r +=
                    cm.At(static_cast<std::size_t>(r), static_cast<std::size_t>(c)) * xm[static_cast<std::size_t>(c)];
            }
            ws.FnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] = fnl_r;
            ws.QnlTraces[static_cast<std::size_t>(r) * ws.SampleCount + m] = qnl_r;
        }

        if (build_jacobian) {
            ws.GTraces.RecordMatrix(gm, n, m, ws.SampleCount);
            ws.CTraces.RecordMatrix(cm, n, m, ws.SampleCount);
        }
    }

    // 4. FFT nonlinear currents and charges into residual
    ws.TimeSeriesScratch.resize(ws.SampleCount);
    for (int r = 0; r < n; ++r) {
        const std::size_t trace_offset = static_cast<std::size_t>(r) * ws.SampleCount;

        // Conductive nonlinear currents
        std::copy_n(ws.FnlTraces.data() + trace_offset, ws.SampleCount, ws.TimeSeriesScratch.data());
        (void)ws.Plan.RealToHarmonics(ws.TimeSeriesScratch, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        for (int slot = 0; slot < c_count; ++slot) {
            const double val = ws.CoeffsScratch[static_cast<std::size_t>(slot)];
            residual[ws.Index(r, slot)] += val;
            row_scale[ws.Index(r, slot)] = std::max(row_scale[ws.Index(r, slot)], std::abs(val));
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
            residual[rc] += ia;
            residual[rs] += ib;
            row_scale[rc] = std::max(row_scale[rc], std::abs(ia));
            row_scale[rs] = std::max(row_scale[rs], std::abs(ib));
        }
    }

    // 5. Stamp harmonic conversion blocks into the Jacobian
    if (build_jacobian && jacobian) {
        const auto stamp_blocks = [&](const ActiveMatrixTraces &traces, const bool apply_omega) {
            for (std::size_t idx = 0; idx < traces.Pairs.size(); ++idx) {
                const auto [r, c] = traces.Pairs[idx];
                std::copy_n(traces.Traces.data() + idx * ws.SampleCount, ws.SampleCount, ws.TimeSeriesScratch.data());
                (void)ws.Plan.RealToNormalizedSpectrum(ws.TimeSeriesScratch, ws.SpectrumScratch);
                StampConversionBlock(*jacobian, static_cast<std::size_t>(r) * static_cast<std::size_t>(c_count),
                                     static_cast<std::size_t>(c) * static_cast<std::size_t>(c_count), ws.Harmonics,
                                     ws.Omega0, ws.SpectrumScratch, apply_omega);
            }
        };
        stamp_blocks(ws.GTraces, false);
        stamp_blocks(ws.CTraces, true);
    }
}

bool CheckHbResidual(const HbWorkspace &ws, const std::vector<double> &residual, const std::vector<double> &row_scale,
                     const ConvergenceOptions &options) {
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const double abstol =
            ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? options.CurrentAbsTol : options.VoltageAbsTol;
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const std::size_t idx = ws.Index(u, slot);
            const double tol = abstol + options.RelTol * row_scale[idx];
            if (std::abs(residual[idx]) > tol) {
                return false;
            }
        }
    }
    return true;
}

double ResidualNormSq(const HbWorkspace &ws, const std::vector<double> &residual) {
    double sum = 0.0;
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const double weight = ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? 1.0 : 1e-3;
        for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
            const double r = residual[ws.Index(u, slot)] * weight;
            sum += r * r;
        }
    }
    return sum;
}

int RunHbNewton(HbWorkspace &ws, const double ac_scale, const double gmin, const ConvergenceOptions &options,
                std::vector<double> &x_hb) {
    const std::size_t total = x_hb.size();
    DenseMatrix jacobian(total, total);
    std::vector<double> residual;
    std::vector<double> row_scale;
    std::vector<double> rhs_neg(total);
    std::vector<double> delta(total);
    std::vector<double> x_cand(total);
    std::vector<double> x_next(total);
    std::vector<std::vector<double>> cand_samples(ws.SampleCount,
                                                  std::vector<double>(static_cast<std::size_t>(ws.UnknownCount), 0.0));
    std::vector<std::vector<double>> prev_samples(ws.SampleCount,
                                                  std::vector<double>(static_cast<std::size_t>(ws.UnknownCount), 0.0));

    for (int iteration = 1; iteration <= options.MaxIterations; ++iteration) {
        EvaluateResidualAndJacobian(ws, x_hb, ac_scale, gmin, true, residual, row_scale, &jacobian);
        const double prev_norm = ResidualNormSq(ws, residual);

        for (std::size_t i = 0; i < total; ++i) {
            rhs_neg[i] = -residual[i];
        }
        if (!jacobian.FactorizeLU() || !jacobian.Solve(rhs_neg, delta)) {
            return 0;
        }

        for (std::size_t i = 0; i < total; ++i) {
            x_cand[i] = x_hb[i] + delta[i];
        }

        // Apply per-sample PN-junction and smooth-logic limiting in the time domain
        UnpackCoefficientsToSamples(ws, x_hb, prev_samples);
        UnpackCoefficientsToSamples(ws, x_cand, cand_samples);
        bool any_limited = false;
        for (std::size_t m = 0; m < ws.SampleCount; ++m) {
            for (const Device *device : ws.NonlinearDevices) {
                if (device->Limit(cand_samples[m], prev_samples[m])) {
                    any_limited = true;
                }
            }
        }

        if (any_limited) {
            PackSamplesToCoefficients(ws, cand_samples, x_next);
        } else {
            x_next = x_cand;
            // Light backtracking line search if an undamped step overshoots
            EvaluateResidualAndJacobian(ws, x_next, ac_scale, gmin, false, residual, row_scale, nullptr);
            if (ResidualNormSq(ws, residual) > 4.0 * prev_norm && prev_norm > 0.0) {
                double alpha = 0.5;
                for (int ls = 0; ls < 4; ++ls) {
                    for (std::size_t i = 0; i < total; ++i) {
                        x_cand[i] = x_hb[i] + alpha * delta[i];
                    }
                    EvaluateResidualAndJacobian(ws, x_cand, ac_scale, gmin, false, residual, row_scale, nullptr);
                    if (ResidualNormSq(ws, residual) < prev_norm) {
                        x_next = x_cand;
                        break;
                    }
                    alpha *= 0.5;
                }
            }
        }

        bool step_converged = iteration >= 2 && !any_limited;
        for (int u = 0; u < ws.UnknownCount && step_converged; ++u) {
            const double abs_tol =
                ws.IsNodeUnknown[static_cast<std::size_t>(u)] ? options.VoltageAbsTol : options.CurrentAbsTol;
            for (int slot = 0; slot < ws.CoeffsPerUnknown; ++slot) {
                const std::size_t idx = ws.Index(u, slot);
                const double tol = options.RelTol * std::abs(x_next[idx]) + abs_tol;
                if (std::abs(x_next[idx] - x_hb[idx]) > tol) {
                    step_converged = false;
                    break;
                }
            }
        }

        x_hb = x_next;

        if (step_converged) {
            EvaluateResidualAndJacobian(ws, x_hb, ac_scale, gmin, false, residual, row_scale, nullptr);
            if (CheckHbResidual(ws, residual, row_scale, options)) {
                return iteration;
            }
        }
    }
    return 0;
}

struct HbSolveOutcome {
    bool Converged = false;
    int Iterations = 0;
    std::string Strategy;
};

HbSolveOutcome SolveHarmonicBalanceNonlinear(HbWorkspace &ws, const HarmonicBalanceConfig &config,
                                             const ConvergenceOptions &options, std::vector<double> &x_hb) {
    HbSolveOutcome outcome;
    std::vector<double> x_dc;
    if (!SolveDcBiasForB0(ws, options, x_dc)) {
        return outcome;
    }

    const auto reset_to_dc = [&]() {
        x_hb.assign(static_cast<std::size_t>(ws.UnknownCount) * static_cast<std::size_t>(ws.CoeffsPerUnknown), 0.0);
        for (int u = 0; u < ws.UnknownCount; ++u) {
            x_hb[ws.Index(u, 0)] = x_dc[static_cast<std::size_t>(u)];
        }
    };

    // Attempt 1: Direct Harmonic Balance Newton from DC operating point
    reset_to_dc();
    if (const int iters = RunHbNewton(ws, 1.0, options.Gmin, options, x_hb)) {
        outcome.Converged = true;
        outcome.Iterations = iters;
        outcome.Strategy = "direct";
        return outcome;
    }

    // Attempt 2: Source amplitude stepping (ramp AC harmonics 0 -> 1 with adaptive step size)
    const int initial_steps = std::max(config.SourceSteps > 0 ? config.SourceSteps : options.SourceSteps, 4);
    const auto try_source_stepping = [&](const double gmin, int &total_iters) {
        double lambda = 0.0;
        double step = 1.0 / static_cast<double>(initial_steps);
        constexpr double MIN_STEP = 1e-4;
        std::vector<double> backup;
        while (lambda < 1.0 - 1e-12) {
            const double next_lambda = std::min(1.0, lambda + step);
            backup = x_hb;
            const int iters = RunHbNewton(ws, next_lambda, gmin, options, x_hb);
            if (iters > 0) {
                total_iters += iters;
                lambda = next_lambda;
                if (iters <= 6) {
                    step = std::min(0.5, step * 1.5);
                }
            } else {
                x_hb = std::move(backup);
                step *= 0.5;
                if (step < MIN_STEP) {
                    return false;
                }
            }
        }
        return true;
    };

    reset_to_dc();
    int total_iters = 0;
    if (try_source_stepping(options.Gmin, total_iters)) {
        outcome.Converged = true;
        outcome.Iterations = total_iters;
        outcome.Strategy = "source stepping";
        return outcome;
    }

    // Attempt 3: Gmin stepping (start at 1e-3 S with source stepping, then lower gmin by decades)
    constexpr double GMIN_START = 1e-3;
    constexpr double GMIN_FACTOR = 0.1;
    reset_to_dc();
    total_iters = 0;
    if (RunHbNewton(ws, 1.0, GMIN_START, options, x_hb) > 0 || try_source_stepping(GMIN_START, total_iters)) {
        bool ok = true;
        for (int step = 1; step <= options.GminSteps; ++step) {
            const double gmin = std::max(GMIN_START * std::pow(GMIN_FACTOR, static_cast<double>(step)), options.Gmin);
            const int iters = RunHbNewton(ws, 1.0, gmin, options, x_hb);
            if (iters == 0) {
                ok = false;
                break;
            }
            total_iters += iters;
        }
        if (ok) {
            outcome.Converged = true;
            outcome.Iterations = total_iters;
            outcome.Strategy = "gmin stepping";
            return outcome;
        }
    }

    return outcome;
}

HarmonicBalanceResult BuildHarmonicBalanceResult(HbWorkspace &ws, const CompiledCircuit &compiled,
                                                 const HarmonicBalanceConfig &config, const std::vector<double> &x_hb,
                                                 const HbSolveOutcome &outcome, std::vector<std::string> warnings) {
    HarmonicBalanceResult result;
    const int harmonics = ws.Harmonics;
    const std::size_t h_count = static_cast<std::size_t>(harmonics + 1);
    const std::size_t sample_count = ws.SampleCount;
    const double period = 1.0 / config.FundamentalFrequency;

    result.Frequencies.resize(h_count);
    for (int k = 0; k <= harmonics; ++k) {
        result.Frequencies[static_cast<std::size_t>(k)] = static_cast<double>(k) * config.FundamentalFrequency;
    }

    result.Times.resize(sample_count);
    for (std::size_t m = 0; m < sample_count; ++m) {
        result.Times[m] = static_cast<double>(m) * period / static_cast<double>(sample_count);
    }

    // Node phasors and waveforms
    const auto node_count = static_cast<std::size_t>(compiled.CircuitNodeCount);
    result.NodePhasors.assign(node_count, std::vector<std::complex<double>>(h_count, {0.0, 0.0}));
    result.NodeWaveforms.assign(node_count, std::vector<double>(sample_count, 0.0));

    ws.CoeffsScratch.resize(static_cast<std::size_t>(ws.CoeffsPerUnknown));
    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        const int u = compiled.System->NodeIndex(node);
        if (u < 0) {
            continue;
        }
        auto &phasors = result.NodePhasors[static_cast<std::size_t>(node)];
        phasors[0] = std::complex<double>(x_hb[ws.Index(u, 0)], 0.0);
        ws.CoeffsScratch[0] = x_hb[ws.Index(u, 0)];
        for (int k = 1; k <= harmonics; ++k) {
            const double ak = x_hb[ws.Index(u, 2 * k - 1)];
            const double bk = x_hb[ws.Index(u, 2 * k)];
            phasors[static_cast<std::size_t>(k)] = std::complex<double>(ak, -bk);
            ws.CoeffsScratch[static_cast<std::size_t>(2 * k - 1)] = ak;
            ws.CoeffsScratch[static_cast<std::size_t>(2 * k)] = bk;
        }
        (void)ws.Plan.HarmonicsToReal(ws.CoeffsScratch, result.NodeWaveforms[static_cast<std::size_t>(node)],
                                      ws.FftScratch);
    }

    // Precompute DC and complex harmonic vectors of all MNA unknowns for linear current extraction
    std::vector<double> x_dc_part(static_cast<std::size_t>(ws.UnknownCount), 0.0);
    std::vector<std::vector<std::complex<double>>> x_harmonics(
        h_count, std::vector<std::complex<double>>(static_cast<std::size_t>(ws.UnknownCount), {0.0, 0.0}));
    for (int u = 0; u < ws.UnknownCount; ++u) {
        x_dc_part[static_cast<std::size_t>(u)] = x_hb[ws.Index(u, 0)];
        x_harmonics[0][static_cast<std::size_t>(u)] = std::complex<double>(x_hb[ws.Index(u, 0)], 0.0);
        for (int k = 1; k <= harmonics; ++k) {
            const double ak = x_hb[ws.Index(u, 2 * k - 1)];
            const double bk = x_hb[ws.Index(u, 2 * k)];
            x_harmonics[static_cast<std::size_t>(k)][static_cast<std::size_t>(u)] = std::complex<double>(ak, -bk);
        }
    }

    // Time-domain samples of MNA unknowns for nonlinear current/charge extraction
    UnpackCoefficientsToSamples(ws, x_hb, ws.StateSamples);
    // Ensure nonlinear devices carry zero residual transient companion state before calling TerminalCurrent
    if (!ws.NonlinearDevices.empty()) {
        ws.SampleG.Clear();
        for (Device *device : ws.NonlinearDevices) {
            device->Stamp(ws.SampleG, x_dc_part, TimeContext{});
        }
    }

    std::vector<double> i_samples(sample_count);
    std::vector<double> q_samples(sample_count);
    std::vector<double> q_coeffs;
    result.CurrentPhasors.resize(compiled.Currents.size());
    result.CurrentWaveforms.resize(compiled.Currents.size());

    for (std::size_t idx = 0; idx < compiled.Currents.size(); ++idx) {
        const CurrentRef &ref = compiled.Currents[idx];
        result.CurrentPhasors[idx].Name = ref.Name;
        result.CurrentPhasors[idx].Phasors.assign(h_count, {0.0, 0.0});
        result.CurrentWaveforms[idx].Name = ref.Name;
        auto &total_phasors = result.CurrentPhasors[idx].Phasors;

        for (const CurrentContribution &contrib : ref.Contributions) {
            if (contrib.Element->IsLinear()) {
                if (const auto *cs = dynamic_cast<const CurrentSourceDevice *>(contrib.Element)) {
                    for (std::size_t m = 0; m < sample_count; ++m) {
                        i_samples[m] = cs->SamplePeriodicCurrent(contrib.Terminal, result.Times[m]);
                    }
                    (void)ws.Plan.RealToHarmonics(i_samples, harmonics, ws.CoeffsScratch, ws.FftScratch);
                    total_phasors[0] += std::complex<double>(ws.CoeffsScratch[0], 0.0);
                    for (int k = 1; k <= harmonics; ++k) {
                        total_phasors[static_cast<std::size_t>(k)] +=
                            std::complex<double>(ws.CoeffsScratch[static_cast<std::size_t>(2 * k - 1)],
                                                 -ws.CoeffsScratch[static_cast<std::size_t>(2 * k)]);
                    }
                } else {
                    total_phasors[0] +=
                        std::complex<double>(contrib.Element->TerminalCurrent(contrib.Terminal, x_dc_part), 0.0);
                    for (int k = 1; k <= harmonics; ++k) {
                        const double kw = static_cast<double>(k) * ws.Omega0;
                        total_phasors[static_cast<std::size_t>(k)] += contrib.Element->AcTerminalCurrent(
                            contrib.Terminal, x_dc_part, x_harmonics[static_cast<std::size_t>(k)], kw);
                    }
                }
            } else {
                for (std::size_t m = 0; m < sample_count; ++m) {
                    i_samples[m] = contrib.Element->TerminalCurrent(contrib.Terminal, ws.StateSamples[m]);
                    q_samples[m] = contrib.Element->TerminalCharge(contrib.Terminal, ws.StateSamples[m]);
                }
                (void)ws.Plan.RealToHarmonics(i_samples, harmonics, ws.CoeffsScratch, ws.FftScratch);
                (void)ws.Plan.RealToHarmonics(q_samples, harmonics, q_coeffs, ws.FftScratch);
                total_phasors[0] += std::complex<double>(ws.CoeffsScratch[0], 0.0);
                for (int k = 1; k <= harmonics; ++k) {
                    const double kw = static_cast<double>(k) * ws.Omega0;
                    const std::complex<double> i_cond(ws.CoeffsScratch[static_cast<std::size_t>(2 * k - 1)],
                                                      -ws.CoeffsScratch[static_cast<std::size_t>(2 * k)]);
                    const std::complex<double> q_k(q_coeffs[static_cast<std::size_t>(2 * k - 1)],
                                                   -q_coeffs[static_cast<std::size_t>(2 * k)]);
                    total_phasors[static_cast<std::size_t>(k)] += i_cond + std::complex<double>(0.0, kw) * q_k;
                }
            }
        }

        ws.CoeffsScratch[0] = total_phasors[0].real();
        for (int k = 1; k <= harmonics; ++k) {
            const std::complex<double> &p = total_phasors[static_cast<std::size_t>(k)];
            ws.CoeffsScratch[static_cast<std::size_t>(2 * k - 1)] = p.real();
            ws.CoeffsScratch[static_cast<std::size_t>(2 * k)] = -p.imag();
        }
        (void)ws.Plan.HarmonicsToReal(ws.CoeffsScratch, result.CurrentWaveforms[idx].Values, ws.FftScratch);
    }

    // Warn if the highest harmonic still carries >1% of the fundamental amplitude (possible truncation)
    if (harmonics >= 2) {
        for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
            const auto &phasors = result.NodePhasors[static_cast<std::size_t>(node)];
            const double v1 = std::abs(phasors[1]);
            const double vh = std::abs(phasors[static_cast<std::size_t>(harmonics)]);
            if (v1 > 1e-6 && vh > 0.01 * v1) {
                warnings.push_back(std::format(
                    "Harmonic {} still carries {:.2f}% of the fundamental at node {}; consider increasing Harmonics",
                    harmonics, 100.0 * vh / v1, node));
                break;
            }
        }
    }

    result.Warnings = std::move(warnings);
    result.Iterations = outcome.Iterations;
    result.Strategy = outcome.Strategy;
    return result;
}

} // namespace

/**
 * @brief   Computes Total Harmonic Distortion (THD) as a dimensionless ratio from harmonic phasors.
 * @param[in] phasors  Complex phasor spectrum indexed `[0 .. H]` where index `1` is the fundamental.
 * @return  $\sqrt{\sum_{k=2}^{H} |X_k|^2} / |X_1|$, or `0.0` when the fundamental is zero or fewer than 3 bins exist.
 */
double ComputeThd(const std::span<const std::complex<double>> phasors) {
    if (phasors.size() < 3) {
        return 0.0;
    }
    const double fundamental = std::abs(phasors[1]);
    if (fundamental <= 0.0) {
        return 0.0;
    }
    double harmonic_power = 0.0;
    for (std::size_t k = 2; k < phasors.size(); ++k) {
        harmonic_power += std::norm(phasors[k]);
    }
    return std::sqrt(harmonic_power) / fundamental;
}

/**
 * @brief   Solves the periodic steady state of a compiled circuit using Alternating Frequency-Time Harmonic Balance.
 * @param[in,out] compiled  Compiled circuit MNA system and devices.
 * @param[in]     settings  Fundamental frequency, harmonic count, and sample configuration.
 * @param[in]     options   Newton-Raphson tolerances and homotopy parameters.
 * @param[out]    messages  Optional diagnostic log receiving convergence status and warnings.
 * @return  Harmonic Balance spectra and one-cycle waveforms, or a diagnostic error message on failure.
 */
std::expected<HarmonicBalanceResult, std::string> ComputeHarmonicBalance(CompiledCircuit &compiled,
                                                                         const HarmonicBalanceConfig &settings,
                                                                         const ConvergenceOptions &options,
                                                                         std::vector<DiagnosticMessage> *messages) {
    const std::expected<std::size_t, std::string> sample_count = ValidateAndResolveSampleCount(settings);
    if (!sample_count) {
        return std::unexpected(sample_count.error());
    }

    std::vector<std::string> warnings;
    std::expected<PartitionedCircuit, std::string> partitioned =
        ValidateAndPartitionDevices(compiled, settings, *sample_count, warnings);
    if (!partitioned) {
        return std::unexpected(partitioned.error());
    }

    const int unknown_count = compiled.System->Size();
    const std::size_t total_unknowns =
        static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(2 * settings.Harmonics + 1);
    if (total_unknowns > MAX_DENSE_UNKNOWNS_WARNING) {
        warnings.push_back(std::format(
            "Harmonic Balance system has {} unknowns ({} MNA x {}); solving dense Jacobian may take noticeable time",
            total_unknowns, unknown_count, 2 * settings.Harmonics + 1));
    }

    std::vector<bool> node_flags(static_cast<std::size_t>(unknown_count), false);
    for (int i = 0; i < unknown_count; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }

    HbWorkspace ws(unknown_count, settings.Harmonics, *sample_count, settings.FundamentalFrequency,
                   std::move(node_flags));
    const double period = 1.0 / settings.FundamentalFrequency;
    AssembleLinearAndExcitation(ws, *partitioned, period);

    std::vector<double> x_hb;
    HbSolveOutcome outcome;

    if (partitioned->NonlinearDevices.empty()) {
        if (!SolveLinearCircuit(ws, x_hb)) {
            return std::unexpected("The Nodalys Harmonic Balance solver encountered a singular linear matrix");
        }
        outcome.Converged = true;
        outcome.Iterations = 1;
        outcome.Strategy = "linear";
    } else {
        ws.NonlinearDevices = partitioned->NonlinearDevices;
        outcome = SolveHarmonicBalanceNonlinear(ws, settings, options, x_hb);
        if (!outcome.Converged) {
            return std::unexpected("Harmonic Balance Newton-Raphson failed to converge for this circuit");
        }
    }

    HarmonicBalanceResult result =
        BuildHarmonicBalanceResult(ws, compiled, settings, x_hb, outcome, std::move(warnings));

    if (messages) {
        std::string summary =
            std::format("nodalys: harmonic balance ({} harmonics, {} samples) converged in {} iterations",
                        settings.Harmonics, *sample_count, outcome.Iterations);
        if (!outcome.Strategy.empty()) {
            summary += std::format(" (with {})", outcome.Strategy);
        }
        messages->push_back({std::move(summary), false});
        for (const std::string &w : result.Warnings) {
            messages->push_back({w, true});
        }
    }

    return result;
}

} // namespace nodalys
