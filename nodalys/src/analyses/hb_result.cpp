/**
 * @file    hb_result.cpp
 * @brief   Turns the harmonics a balance found into spectra, waveforms and the currents of the circuit.
 */

#include "analyses/hb_result.h"

#include "analyses/hb_spectrum.h"
#include <cmath>
#include <complex>
#include <format>
#include <optional>
#include <span>
#include <utility>

namespace Nodalys {

namespace {

/// Smallest fundamental, in volts, that the truncation check compares the highest harmonic with.
constexpr double MIN_FUNDAMENTAL_FOR_TRUNCATION = 1e-6;
/// Share of the fundamental above which the highest harmonic is taken to be cut off.
constexpr double TRUNCATION_SHARE = 0.01;
constexpr double PERCENT = 100.0;

/// The state of all unknowns as the DC values and the complex phasor of each harmonic, for the linear devices.
struct UnknownHarmonics {
    std::vector<double> Dc;
    /// `[harmonic][unknown]`, with the DC value at harmonic zero.
    std::vector<std::vector<std::complex<double>>> Phasors;
};

/// What the extraction of the current of a device reads and reuses between contributions.
struct CurrentContext {
    HbWorkspace &Ws;
    const std::vector<double> &Times;
    const UnknownHarmonics &Unknowns;
    std::vector<double> CurrentSamples;
    std::vector<double> ChargeSamples;
    std::vector<double> ChargeCoeffs;
};

void FillAxes(const HbWorkspace &ws, const HarmonicBalanceConfig &config, HarmonicBalanceResult &result) {
    const double period = 1.0 / config.FundamentalFrequency;

    result.Frequencies.resize(static_cast<std::size_t>(ws.Harmonics) + 1);
    for (int k = 0; k <= ws.Harmonics; ++k) {
        result.Frequencies[static_cast<std::size_t>(k)] = static_cast<double>(k) * config.FundamentalFrequency;
    }

    result.Times.resize(ws.SampleCount);
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        result.Times[m] = static_cast<double>(m) * period / static_cast<double>(ws.SampleCount);
    }
}

void FillNodeSpectra(HbWorkspace &ws,
                     const CompiledCircuit &compiled,
                     const std::vector<double> &x_hb,
                     HarmonicBalanceResult &result) {
    const auto h_count = static_cast<std::size_t>(ws.Harmonics) + 1;
    const auto node_count = static_cast<std::size_t>(compiled.CircuitNodeCount);
    result.NodePhasors.assign(node_count, std::vector<std::complex<double>>(h_count, {0.0, 0.0}));
    result.NodeWaveforms.assign(node_count, std::vector<double>(ws.SampleCount, 0.0));

    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        const int u = compiled.System->NodeIndex(node);
        if (u < 0) {
            continue;
        }
        const std::span<const double> coeffs =
            std::span(x_hb).subspan(ws.Index(u, 0), static_cast<std::size_t>(ws.CoeffsPerUnknown));
        result.NodePhasors[static_cast<std::size_t>(node)] = CoefficientsToPhasors(coeffs);
        ws.CoeffsScratch.assign(coeffs.begin(), coeffs.end());
        (void)ws.Plan.HarmonicsToReal(ws.CoeffsScratch,
                                      result.NodeWaveforms[static_cast<std::size_t>(node)],
                                      ws.FftScratch);
    }
}

UnknownHarmonics FindUnknownHarmonics(const HbWorkspace &ws, const std::vector<double> &x_hb) {
    const auto h_count = static_cast<std::size_t>(ws.Harmonics) + 1;
    UnknownHarmonics unknowns;
    unknowns.Dc.assign(static_cast<std::size_t>(ws.UnknownCount), 0.0);
    unknowns.Phasors.assign(h_count,
                            std::vector<std::complex<double>>(static_cast<std::size_t>(ws.UnknownCount), {0.0, 0.0}));
    for (int u = 0; u < ws.UnknownCount; ++u) {
        unknowns.Dc[static_cast<std::size_t>(u)] = x_hb[ws.Index(u, 0)];
        const std::span<const double> coeffs =
            std::span(x_hb).subspan(ws.Index(u, 0), static_cast<std::size_t>(ws.CoeffsPerUnknown));
        const std::vector<std::complex<double>> phasors = CoefficientsToPhasors(coeffs);
        for (std::size_t k = 0; k < h_count; ++k) {
            unknowns.Phasors[k][static_cast<std::size_t>(u)] = phasors[k];
        }
    }
    return unknowns;
}

/// Puts the nonlinear devices at the state, so the current and charge they report are those of the solution.
void PrepareDeviceStates(HbWorkspace &ws, const std::vector<double> &x_hb, const std::vector<double> &x_dc) {
    UnpackCoefficientsToSamples(ws, x_hb, ws.StateSamples);
    // Ensure nonlinear devices carry zero residual transient companion state before calling TerminalCurrent
    if (!ws.NonlinearDevices.empty()) {
        ws.SampleG.Clear();
        for (Device *device : ws.NonlinearDevices) {
            device->Stamp(ws.SampleG, x_dc, TimeContext{});
        }
    }
}

/// The phasors of the current an independent source drives, from its waveform over the period.
std::vector<std::complex<double>>
FindSourcePhasors(CurrentContext &context, const CurrentSourceDevice &source, const int terminal) {
    HbWorkspace &ws = context.Ws;
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        context.CurrentSamples[m] = source.SamplePeriodicCurrent(terminal, context.Times[m]);
    }
    (void)ws.Plan.RealToHarmonics(context.CurrentSamples, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
    return CoefficientsToPhasors(ws.CoeffsScratch);
}

/// The phasors of the current of a linear device, from its small-signal relation to the state.
std::vector<std::complex<double>>
FindLinearDevicePhasors(const CurrentContext &context, const Device &element, const int terminal) {
    const HbWorkspace &ws = context.Ws;
    std::vector<std::complex<double>> phasors(static_cast<std::size_t>(ws.Harmonics) + 1);
    phasors[0] = std::complex<double>(element.TerminalCurrent(terminal, context.Unknowns.Dc), 0.0);
    for (int k = 1; k <= ws.Harmonics; ++k) {
        const double kw = static_cast<double>(k) * ws.Omega0;
        phasors[static_cast<std::size_t>(k)] =
            element.AcTerminalCurrent(terminal,
                                      context.Unknowns.Dc,
                                      context.Unknowns.Phasors[static_cast<std::size_t>(k)],
                                      kw);
    }
    return phasors;
}

/// The phasors of the current of a nonlinear device: its conduction current plus the displacement current of its
/// charge.
std::vector<std::complex<double>>
FindNonlinearDevicePhasors(CurrentContext &context, const Device &element, const int terminal) {
    HbWorkspace &ws = context.Ws;
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        context.CurrentSamples[m] = element.TerminalCurrent(terminal, ws.StateSamples[m]);
        context.ChargeSamples[m] = element.TerminalCharge(terminal, ws.StateSamples[m]);
    }
    (void)ws.Plan.RealToHarmonics(context.CurrentSamples, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
    (void)ws.Plan.RealToHarmonics(context.ChargeSamples, ws.Harmonics, context.ChargeCoeffs, ws.FftScratch);
    const std::vector<std::complex<double>> conduction = CoefficientsToPhasors(ws.CoeffsScratch);
    const std::vector<std::complex<double>> charge = CoefficientsToPhasors(context.ChargeCoeffs);

    std::vector<std::complex<double>> phasors(conduction.size());
    phasors[0] = conduction[0];
    for (int k = 1; k <= ws.Harmonics; ++k) {
        const double kw = static_cast<double>(k) * ws.Omega0;
        const auto index = static_cast<std::size_t>(k);
        phasors[index] = conduction[index] + std::complex<double>(0.0, kw) * charge[index];
    }
    return phasors;
}

std::vector<std::complex<double>> FindContributionPhasors(CurrentContext &context,
                                                          const CurrentContribution &contribution) {
    if (!contribution.Element->IsLinear()) {
        return FindNonlinearDevicePhasors(context, *contribution.Element, contribution.Terminal);
    }
    if (const auto *source = dynamic_cast<const CurrentSourceDevice *>(contribution.Element)) {
        return FindSourcePhasors(context, *source, contribution.Terminal);
    }
    return FindLinearDevicePhasors(context, *contribution.Element, contribution.Terminal);
}

void FillCurrents(HbWorkspace &ws,
                  const CompiledCircuit &compiled,
                  const UnknownHarmonics &unknowns,
                  HarmonicBalanceResult &result) {
    const auto h_count = static_cast<std::size_t>(ws.Harmonics) + 1;
    CurrentContext context{.Ws = ws,
                           .Times = result.Times,
                           .Unknowns = unknowns,
                           .CurrentSamples = std::vector<double>(ws.SampleCount),
                           .ChargeSamples = std::vector<double>(ws.SampleCount),
                           .ChargeCoeffs = {}};
    result.CurrentPhasors.resize(compiled.Currents.size());
    result.CurrentWaveforms.resize(compiled.Currents.size());

    for (std::size_t idx = 0; idx < compiled.Currents.size(); ++idx) {
        const CurrentRef &ref = compiled.Currents[idx];
        result.CurrentPhasors[idx].Name = ref.Name;
        result.CurrentPhasors[idx].Phasors.assign(h_count, {0.0, 0.0});
        result.CurrentWaveforms[idx].Name = ref.Name;
        auto &total_phasors = result.CurrentPhasors[idx].Phasors;

        for (const CurrentContribution &contribution : ref.Contributions) {
            const std::vector<std::complex<double>> phasors = FindContributionPhasors(context, contribution);
            for (std::size_t k = 0; k < h_count; ++k) {
                total_phasors[k] += phasors[k];
            }
        }

        const std::vector<double> coeffs = PhasorsToCoefficients(total_phasors);
        (void)ws.Plan.HarmonicsToReal(coeffs, result.CurrentWaveforms[idx].Values, ws.FftScratch);
    }
}

/// Finds the first node whose highest harmonic still carries a share of the fundamental, which hints at a truncated
/// expansion.
std::optional<std::string>
FindTruncationWarning(const HarmonicBalanceResult &result, const int node_count, const int harmonics) {
    if (harmonics < 2) {
        return std::nullopt;
    }
    for (int node = 1; node < node_count; ++node) {
        const auto &phasors = result.NodePhasors[static_cast<std::size_t>(node)];
        const double v1 = std::abs(phasors[1]);
        const double vh = std::abs(phasors[static_cast<std::size_t>(harmonics)]);
        if (v1 > MIN_FUNDAMENTAL_FOR_TRUNCATION && vh > TRUNCATION_SHARE * v1) {
            return std::format(
                "Harmonic {} still carries {:.2f}% of the fundamental at node {}; consider increasing Harmonics",
                harmonics,
                PERCENT * vh / v1,
                node);
        }
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Assembles the result of a balance from the harmonics of the unknowns.
 * @param[in,out] ws        Workspace whose scratch buffers are used, and whose nonlinear devices are stamped.
 * @param[in]     compiled  The circuit, for its nodes and the currents to report.
 * @param[in]     config    Fundamental frequency of the balance.
 * @param[in]     x_hb      The harmonics of all unknowns.
 * @param[in]     outcome   How the balance converged.
 * @param[in]     warnings  The warnings found before the solve, which the result carries on.
 * @return  The frequencies, node spectra and waveforms, and the spectra and waveforms of the currents.
 */
HarmonicBalanceResult BuildHarmonicBalanceResult(HbWorkspace &ws,
                                                 const CompiledCircuit &compiled,
                                                 const HarmonicBalanceConfig &config,
                                                 const std::vector<double> &x_hb,
                                                 const HbSolveOutcome &outcome,
                                                 std::vector<std::string> warnings) {
    HarmonicBalanceResult result;
    FillAxes(ws, config, result);
    FillNodeSpectra(ws, compiled, x_hb, result);

    // The linear devices read the harmonics of every unknown, the nonlinear ones its time samples
    const UnknownHarmonics unknowns = FindUnknownHarmonics(ws, x_hb);
    PrepareDeviceStates(ws, x_hb, unknowns.Dc);
    FillCurrents(ws, compiled, unknowns, result);

    if (std::optional<std::string> warning = FindTruncationWarning(result, compiled.CircuitNodeCount, ws.Harmonics)) {
        warnings.push_back(std::move(*warning));
    }

    result.Warnings = std::move(warnings);
    result.Iterations = outcome.Iterations;
    result.Strategy = outcome.Strategy;
    return result;
}

} // namespace Nodalys
