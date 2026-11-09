/**
 * @file    ac_analysis.cpp
 * @brief   Small-signal AC frequency sweep (`ComputeACSweep`) of the Nodalys solver for Bode magnitude and phase plots.
 * @details
 * ### How Small-Signal AC Analysis Works
 * 1. **DC Operating Point ($x_{\text{dc}}$)**: Solves the nonlinear DC equations via `SolveDc()`. On return,
 *    `compiled.System` holds the exact linearized real Jacobian $A_{\text{DC}}(x_{\text{dc}})$ (conductances,
 *    transconductances, series resistances, op-amp macromodels, and voltage/inductor branch incidences).
 * 2. **Reactive & AC Stimulus Stamp**: Each device stamps its frequency-independent reactive coefficients ($+C$ for
 *    capacitors and semiconductor junction/diffusion capacitances, $-L$ and $-M$ for inductors and transformers) into
 *    a peer `MnaSystem reactive` ($C_{\text{AC}}$) and its AC source magnitude into $b_{\text{AC}}$.
 * 3. **Frequency Sweep**: At each decade-spaced frequency $f_k = f_{\text{start}} \cdot 10^{k / N_{\text{dec}}}$
 *    ($\omega_k = 2\pi f_k$), solves the complex linear system:
 *    $$\left(A_{\text{DC}}(x_{\text{dc}}) + j\omega_k C_{\text{AC}}(x_{\text{dc}})\right) x_{\text{ac}}(\omega_k) = b_{\text{AC}}$$
 *    and converts node voltages and component currents into decibels ($20 \log_{10} \max(|z|, 10^{-15})$) and phase
 *    degrees ($\arg(z) \cdot 180 / \pi$).
 */

#include "native/analyses.h"

#include "native/devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>

namespace Core::Native {

std::expected<ACSweep, std::string> ComputeACSweep(CompiledCircuit &compiled, const ACSweepSettings &settings,
                                                   const ConvergenceOptions &options,
                                                   std::vector<SimulatorMessage> &messages) {
    if (settings.StartFrequency <= 0.0 || settings.StopFrequency < settings.StartFrequency ||
        settings.PointsPerDecade <= 0) {
        return std::unexpected("The AC sweep has invalid frequency settings; check start, stop and points per decade");
    }

    // Step 1: Solve the DC operating point to linearize all nonlinear devices around x_dc
    const int size = compiled.System->Size();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_dc, options, TimeContext{});
    if (!outcome.Converged) {
        return std::unexpected("The native solver found no operating point to linearize the AC sweep around");
    }
    DescribeOutcome(outcome, "operating point", messages);

    // Step 2: Stamp the frequency-independent reactive matrix C_ac and AC excitation vector b_ac
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    // Step 3: Build the decade-logarithmic frequency grid, matching ngspice (`ac dec <PointsPerDecade> <Start> <Stop>`)
    ACSweep result;
    for (int index = 0;; ++index) {
        const double frequency =
            settings.StartFrequency *
            std::pow(10.0, static_cast<double>(index) / static_cast<double>(settings.PointsPerDecade));
        if (frequency > settings.StopFrequency * (1.0 + 1e-9)) {
            break;
        }
        result.Frequencies.push_back(std::min(frequency, settings.StopFrequency));
        if (frequency >= settings.StopFrequency * (1.0 - 1e-9)) {
            break;
        }
    }

    const std::size_t point_count = result.Frequencies.size();
    result.NodeMagnitudesDecibels.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
    result.NodePhasesDegrees.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        result.NodeMagnitudesDecibels[static_cast<std::size_t>(node)].reserve(point_count);
        result.NodePhasesDegrees[static_cast<std::size_t>(node)].reserve(point_count);
    }
    result.CurrentMagnitudesDecibels.resize(compiled.Currents.size());
    result.CurrentPhasesDegrees.resize(compiled.Currents.size());
    for (std::size_t index = 0; index < compiled.Currents.size(); ++index) {
        result.CurrentMagnitudesDecibels[index].Name = compiled.Currents[index].Name;
        result.CurrentMagnitudesDecibels[index].Values.reserve(point_count);
        result.CurrentPhasesDegrees[index].Name = compiled.Currents[index].Name;
        result.CurrentPhasesDegrees[index].Values.reserve(point_count);
    }

    constexpr double MIN_MAGNITUDE = 1e-15; // Floor at -300 dB to match ngspice reader
    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;
    std::vector<std::complex<double>> x_ac(static_cast<std::size_t>(size));

    for (const double frequency : result.Frequencies) {
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAc(reactive, omega, x_ac)) {
            return std::unexpected(std::format("The native AC solver encountered a singular matrix at f = {} Hz",
                                               frequency));
        }

        for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
            const int unknown = compiled.System->NodeIndex(node);
            const std::complex<double> v =
                unknown < 0 ? std::complex<double>{0.0, 0.0} : x_ac[static_cast<std::size_t>(unknown)];
            const double mag_db = 20.0 * std::log10(std::max(std::abs(v), MIN_MAGNITUDE));
            const double phase_deg = std::arg(v) * RAD_TO_DEG;
            result.NodeMagnitudesDecibels[static_cast<std::size_t>(node)].push_back(mag_db);
            result.NodePhasesDegrees[static_cast<std::size_t>(node)].push_back(phase_deg);
        }

        for (std::size_t index = 0; index < compiled.Currents.size(); ++index) {
            std::complex<double> current{0.0, 0.0};
            for (const CurrentContribution &contribution : compiled.Currents[index].Contributions) {
                current += contribution.Element->AcTerminalCurrent(contribution.Terminal, x_dc, x_ac, omega);
            }
            const double mag_db = 20.0 * std::log10(std::max(std::abs(current), MIN_MAGNITUDE));
            const double phase_deg = std::arg(current) * RAD_TO_DEG;
            result.CurrentMagnitudesDecibels[index].Values.push_back(mag_db);
            result.CurrentPhasesDegrees[index].Values.push_back(phase_deg);
        }
    }

    messages.push_back({std::format("native: AC sweep computed {} frequency points", point_count), false});
    return result;
}

} // namespace Core::Native
