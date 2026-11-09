/**
 * @file    noise_analysis.cpp
 * @brief   Small-signal AC noise spectral density and integrated RMS noise analysis (`ComputeNoise`, SPICE `.noise`)
 *          of the Nodalys solver using the transposed complex adjoint method.
 * @details
 * ### How Small-Signal Adjoint Noise Analysis Works
 * Given an output node $n_{\text{out}}$, an input AC source $S_{\text{in}}$, and a decade frequency grid $\{f_k\}$:
 * 1. **DC Operating Point ($\mathbf{x}_{\text{dc}}$)**: Solves the nonlinear DC equations $\mathbf{F}(\mathbf{x}_{\text{dc}}) = \mathbf{0}$
 *    via `SolveDc()`. On return, `compiled.System` holds the real small-signal Jacobian $A_{\text{DC}}(\mathbf{x}_{\text{dc}})$.
 * 2. **Reactive Matrix Stamp ($C_{\text{AC}}$)**: Each device stamps its frequency-independent reactive sensitivities
 *    into a companion `MnaSystem reactive`.
 * 3. **Single Adjoint Solve per Frequency ($\omega_k = 2\pi f_k$)**:
 *    Instead of solving one linear system per noisy resistor/semiconductor, we solve the **transposed** complex system
 *    once per frequency:
 *    $$\left(A_{\text{DC}}(\mathbf{x}_{\text{dc}}) + j\omega_k C_{\text{AC}}(\mathbf{x}_{\text{dc}})\right)^T \boldsymbol{\lambda}(\omega_k) = \mathbf{e}_{n_{\text{out}}}$$
 *    By Tellegen's theorem / linear algebra duality ($\boldsymbol{\lambda}^T = \mathbf{e}_{n_{\text{out}}}^T \mathbf{Y}(j\omega_k)^{-1}$):
 *    - Any uncorrelated current noise source $i_n$ connected between nodes $a$ and $b$ produces an output voltage
 *      mean-square spectral density:
 *      $$S_{v,\text{out}}^{(i_n)}(f_k) = S_{i_n}(f_k) \cdot \left|\lambda_a(\omega_k) - \lambda_b(\omega_k)\right|^2 \quad (\text{V}^2/\text{Hz})$$
 *    - Simultaneously, the small-signal transfer gain $G_{\text{in}}(j\omega_k) = \tilde{V}_{n_{\text{out}}} / \tilde{S}_{\text{in}}$
 *      from the input source $S_{\text{in}}$ to the output node is obtained directly from $\boldsymbol{\lambda}(\omega_k)$
 *      without a second matrix solve:
 *      $$G_{\text{in}}(j\omega_k) = \begin{cases} \lambda_{b_{\text{in}}}(\omega_k) & \text{voltage source on branch } b_{\text{in}} \\ \lambda_{n_-}(\omega_k) - \lambda_{n_+}(\omega_k) & \text{current source from } n_+ \text{ to } n_- \end{cases}$$
 *    - The input-referred noise power spectral density is then:
 *      $$S_{\text{in}}(f_k) = \frac{S_{\text{out}}(f_k)}{\left|G_{\text{in}}(j\omega_k)\right|^2}$$
 * 4. **SPICE3f5 Log-Log Power-Law Integration (`Nintegrate`)**:
 *    Over each frequency interval $[f_{k-1}, f_k]$, the noise power spectral density $S(f)$ is modeled locally as a
 *    power law $S(f) = S(f_{k-1}) (f / f_{k-1})^\alpha$ with $\alpha = \ln(S_k / S_{k-1}) / \ln(f_k / f_{k-1})$, which
 *    integrates both white noise ($\alpha = 0$), $1/f$ flicker noise ($\alpha = -1$), and single-pole RC roll-off
 *    ($\alpha = -2$) analytically:
 *    $$\int_{f_{k-1}}^{f_k} S(f)\,df = \begin{cases} S_k f_k \ln(f_k / f_{k-1}) & |\alpha + 1| < 10^{-10} \\ \dfrac{S_k f_k - S_{k-1} f_{k-1}}{\alpha + 1} & \text{otherwise} \end{cases}$$
 */

#include "native/analyses.h"

#include "native/devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <unordered_map>

namespace Core::Native {

namespace {

/// Floor power spectral density ($10^{-38}\text{ V}^2/\text{Hz}$) used by SPICE3f5 `Nintegrate` when taking logarithms.
constexpr double MIN_NOISE_VARIANCE = 1e-38;
/// Floor squared transfer gain to prevent division by zero when input-referring noise through a transmission zero.
constexpr double MIN_GAIN_SQUARED = 1e-38;

/**
 * @brief   Integrates a noise power spectral density $S(f)$ over $[f_1, f_2]$ using the SPICE3f5 log-log power-law rule.
 */
double IntegrateNoiseStep(const double f1, const double f2, const double s1, const double s2) {
    if (s1 <= 0.0 && s2 <= 0.0) {
        return 0.0;
    }
    const double d_ln_f = std::log(f2 / f1);
    if (std::fabs(d_ln_f) < 1e-15) {
        return 0.5 * (s1 + s2) * (f2 - f1);
    }
    const double clamped_s1 = std::max(s1, MIN_NOISE_VARIANCE);
    const double clamped_s2 = std::max(s2, MIN_NOISE_VARIANCE);
    const double exponent = std::log(clamped_s2 / clamped_s1) / d_ln_f;
    if (std::fabs(exponent + 1.0) < 1e-10) {
        return clamped_s2 * f2 * d_ln_f;
    }
    return (clamped_s2 * f2 - clamped_s1 * f1) / (exponent + 1.0);
}

} // namespace

std::expected<Noise, std::string> ComputeNoise(const Circuit &circuit, CompiledCircuit &compiled,
                                               const NoiseSettings &settings, const ConvergenceOptions &options,
                                               std::vector<SimulatorMessage> &messages) {
    if (settings.StartFrequency <= 0.0 || settings.StopFrequency <= settings.StartFrequency ||
        settings.PointsPerDecade <= 0) {
        return std::unexpected("The noise analysis has invalid frequency settings; check start, stop and points per decade");
    }
    if (settings.OutputNode <= 0 || settings.OutputNode >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no output node {}", settings.OutputNode));
    }

    const auto source_it = compiled.SourcesByName.find(settings.InputSource);
    if (source_it == compiled.SourcesByName.end()) {
        const bool exists = std::ranges::any_of(circuit.GetEntries(), [&](const CircuitEntry &entry) {
            return entry.Part->GetName() == settings.InputSource;
        });
        return std::unexpected(
            exists ? std::format("The source {} cannot be the input of a noise analysis", settings.InputSource)
                   : std::format("The circuit has no source named {}", settings.InputSource));
    }

    const auto *voltage_source = dynamic_cast<const VoltageSourceDevice *>(source_it->second);
    const auto *current_source = dynamic_cast<const CurrentSourceDevice *>(source_it->second);
    if (voltage_source == nullptr && current_source == nullptr) {
        return std::unexpected(std::format("The source {} cannot be the input of a noise analysis", settings.InputSource));
    }

    // Step 1: Solve the DC operating point to linearize all nonlinear devices around x_dc
    const int size = compiled.System->Size();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_dc, options, TimeContext{});
    if (!outcome.Converged) {
        return std::unexpected("The native solver found no operating point to linearize the noise analysis around");
    }
    DescribeOutcome(outcome, "operating point", messages);

    // Step 2: Stamp the frequency-independent reactive matrix C_ac
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    // Step 3: Build the decade-logarithmic frequency grid matching ngspice (`.noise ... dec <N> <Start> <Stop>`)
    Noise result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = voltage_source != nullptr ? "V" : "A";
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
    result.OutputDensities.assign(point_count, 0.0);
    result.InputDensities.assign(point_count, 0.0);

    const std::size_t device_count = compiled.Devices.size();
    std::unordered_map<const Device *, std::size_t> device_index;
    device_index.reserve(device_count);
    for (std::size_t d = 0; d < device_count; ++d) {
        device_index[compiled.Devices[d].get()] = d;
    }

    std::vector<double> prev_device_out_var(device_count, 0.0);
    std::vector<double> integrated_device_out_var(device_count, 0.0);
    double integrated_total_out_var = 0.0;
    double integrated_total_in_var = 0.0;

    const std::size_t group_count = compiled.NoiseGroups.size();
    std::vector<std::vector<double>> group_variances(group_count, std::vector<double>(point_count, 0.0));

    const int out_unknown = compiled.System->NodeIndex(settings.OutputNode);
    std::vector<std::complex<double>> adjoint(static_cast<std::size_t>(size));

    // Step 4: Sweep frequency and solve the transposed adjoint system at each point
    for (std::size_t k = 0; k < point_count; ++k) {
        const double frequency = result.Frequencies[k];
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAcTranspose(reactive, omega, out_unknown, adjoint)) {
            return std::unexpected(std::format("The native noise solver encountered a singular matrix at f = {} Hz",
                                               frequency));
        }

        // Compute the small-signal transfer gain G_in(jw) from the input source to the output node via the adjoint vector
        std::complex<double> gain{0.0, 0.0};
        if (voltage_source != nullptr) {
            gain = adjoint[static_cast<std::size_t>(voltage_source->Branch())];
        } else {
            const std::complex<double> la =
                current_source->NodeA() < 0 ? std::complex<double>{0.0, 0.0}
                                            : adjoint[static_cast<std::size_t>(current_source->NodeA())];
            const std::complex<double> lb =
                current_source->NodeB() < 0 ? std::complex<double>{0.0, 0.0}
                                            : adjoint[static_cast<std::size_t>(current_source->NodeB())];
            gain = lb - la;
        }
        const double inv_gain_sq = 1.0 / std::max(std::norm(gain), MIN_GAIN_SQUARED);

        double total_out_var = 0.0;
        for (std::size_t d = 0; d < device_count; ++d) {
            const double out_var = compiled.Devices[d]->OutputNoiseVariance(x_dc, adjoint, frequency);
            total_out_var += out_var;
            if (k > 0) {
                const double step_out =
                    IntegrateNoiseStep(result.Frequencies[k - 1], frequency, prev_device_out_var[d], out_var);
                // Matches SPICE3f5 / ngspice (`resnoise.c`, `bjtnoise.c`, etc.): each step's integrated output
                // variance `tempOnoise` is referred to the input by multiplying by `data->GainSqInv` at the step end.
                const double step_in = step_out * inv_gain_sq;
                integrated_device_out_var[d] += step_out;
                integrated_total_out_var += step_out;
                integrated_total_in_var += step_in;
            }
            prev_device_out_var[d] = out_var;
        }

        result.OutputDensities[k] = std::sqrt(total_out_var);
        result.InputDensities[k] = std::sqrt(total_out_var * inv_gain_sq);

        for (std::size_t g = 0; g < group_count; ++g) {
            double g_var = 0.0;
            for (const Device *dev : compiled.NoiseGroups[g].Devices) {
                const auto it = device_index.find(dev);
                if (it != device_index.end()) {
                    g_var += prev_device_out_var[it->second];
                }
            }
            group_variances[g][k] = g_var;
        }
    }

    result.OutputTotal = std::sqrt(integrated_total_out_var);
    result.InputTotal = std::sqrt(integrated_total_in_var);

    // Step 5: Assemble per-component noise contributions and sort descending by total output RMS noise
    for (std::size_t g = 0; g < group_count; ++g) {
        double group_total_var = 0.0;
        for (const Device *dev : compiled.NoiseGroups[g].Devices) {
            const auto it = device_index.find(dev);
            if (it != device_index.end()) {
                group_total_var += integrated_device_out_var[it->second];
            }
        }
        if (group_total_var <= 0.0) {
            continue;
        }
        NoiseContribution contribution;
        contribution.Name = compiled.NoiseGroups[g].Name;
        contribution.OutputDensities.resize(point_count);
        for (std::size_t k = 0; k < point_count; ++k) {
            contribution.OutputDensities[k] = std::sqrt(group_variances[g][k]);
        }
        contribution.OutputTotal = std::sqrt(group_total_var);
        result.Contributions.push_back(std::move(contribution));
    }
    std::ranges::stable_sort(result.Contributions, std::ranges::greater{}, &NoiseContribution::OutputTotal);

    messages.push_back({std::format("native: noise analysis computed {} frequency points", point_count), false});
    return result;
}

} // namespace Core::Native
