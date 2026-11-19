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

#include "analyses/hb_result.h"
#include "analyses/hb_setup.h"
#include "analyses/hb_solve.h"
#include "analyses/hb_workspace.h"
#include <cmath>
#include <complex>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/// Solves the balance with the strategy that fits the circuit, and fills the harmonics of all unknowns.
std::expected<HbSolveOutcome, std::string> SolveBalance(HbWorkspace &ws,
                                                        const PartitionedCircuit &partitioned,
                                                        const HarmonicBalanceConfig &settings,
                                                        const ConvergenceOptions &options,
                                                        std::vector<double> &x_hb) {
    if (partitioned.NonlinearDevices.empty()) {
        if (!SolveLinearCircuit(ws, x_hb)) {
            return std::unexpected("The Nodalys Harmonic Balance solver encountered a singular linear matrix");
        }
        return HbSolveOutcome{.Converged = true, .Iterations = 1, .Strategy = "linear"};
    }

    ws.NonlinearDevices = partitioned.NonlinearDevices;
    ws.Nodesets = partitioned.Nodesets;
    HbSolveOutcome outcome = SolveHarmonicBalanceNonlinear(ws, settings, options, x_hb);
    if (ws.IsStopped) {
        return std::unexpected("The harmonic balance was stopped before it converged");
    }
    if (!outcome.Converged) {
        std::string failure = "Harmonic Balance Newton-Raphson failed to converge for this circuit";
        if (!ws.Fault.empty()) {
            failure += std::format("; {}", ws.Fault);
        }
        return std::unexpected(std::move(failure));
    }
    return outcome;
}

/// Reports the convergence and the warnings of a balance to the diagnostic log.
void ReportOutcome(const HarmonicBalanceConfig &settings,
                   const std::size_t sample_count,
                   const HbSolveOutcome &outcome,
                   const HarmonicBalanceResult &result,
                   std::vector<DiagnosticMessage> &messages) {
    std::string summary = std::format("nodalys: harmonic balance ({} harmonics, {} samples) converged in {} iterations",
                                      settings.Harmonics,
                                      sample_count,
                                      outcome.Iterations);
    if (!outcome.Strategy.empty()) {
        summary += std::format(" (with {})", outcome.Strategy);
    }
    messages.push_back({std::move(summary), false});
    for (const std::string &w : result.Warnings) {
        messages.push_back({w, true});
    }
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
    if (const std::expected<void, std::string> devices = ValidateDevices(compiled, settings); !devices) {
        return std::unexpected(devices.error());
    }
    const PartitionedCircuit partitioned = PartitionDevices(compiled);

    std::vector<std::string> warnings;
    if (std::optional<std::string> warning = FindSharpEdgeWarning(compiled, settings, *sample_count)) {
        warnings.push_back(std::move(*warning));
    }
    if (const std::expected<void, std::string> size =
            ValidateSystemSize(compiled.System->GetSize(), settings.Harmonics, warnings);
        !size) {
        return std::unexpected(size.error());
    }

    HbWorkspace ws = MakeHbWorkspace(compiled, settings, *sample_count);
    AssembleLinearAndExcitation(ws, partitioned, 1.0 / settings.FundamentalFrequency);

    std::vector<double> x_hb;
    const std::expected<HbSolveOutcome, std::string> outcome = SolveBalance(ws, partitioned, settings, options, x_hb);
    if (!outcome) {
        return std::unexpected(outcome.error());
    }

    HarmonicBalanceResult result =
        BuildHarmonicBalanceResult(ws, compiled, settings, x_hb, *outcome, std::move(warnings));
    if (messages) {
        ReportOutcome(settings, *sample_count, *outcome, result, *messages);
    }
    return result;
}

} // namespace Nodalys
