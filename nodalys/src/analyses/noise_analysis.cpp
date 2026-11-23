/**
 * @file    noise_analysis.cpp
 * @brief   Frequency-domain thermal and shot noise spectral density solver.
 */

#include "analyses/analyses.h"

#include "analyses/analysis_preamble.h"
#include "analyses/frequency_sweep.h"
#include "devices/device.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Nodalys {

namespace {

constexpr double MIN_NOISE_VARIANCE = 1e-38;
constexpr double MIN_GAIN_SQUARED = 1e-38;

/// Below this change of the logarithm of the frequency, two points are one point and the step is a trapezoid.
constexpr double MIN_LOG_FREQUENCY_STEP = 1e-12;

/// How close the slope of the noise density in a log-log plot may come to -1, where the integral is a logarithm.
constexpr double INVERSE_SLOPE_TOLERANCE = 1e-10;

/**
 * @struct  NoiseAccumulator
 * @brief   What the sweep carries from one frequency to the next: the variance of each device at the previous point,
 *          its integral so far, and the variance of each group at every point.
 */
struct NoiseAccumulator {
    std::unordered_map<const Device *, std::size_t> DeviceIndex;
    std::vector<double> PreviousDeviceVariances;
    std::vector<double> IntegratedDeviceVariances;
    double IntegratedOutputVariance = 0.0;
    double IntegratedInputVariance = 0.0;
    std::vector<std::vector<double>> GroupVariances;
};

double IntegrateNoiseStep(const double f1, const double f2, const double s1, const double s2) {
    if (s1 <= 0.0 && s2 <= 0.0) {
        return 0.0;
    }
    const double d_ln_f = std::log(f2 / f1);
    if (std::fabs(d_ln_f) < MIN_LOG_FREQUENCY_STEP) {
        return 0.5 * (s1 + s2) * (f2 - f1);
    }
    if (s1 <= MIN_NOISE_VARIANCE && s2 <= MIN_NOISE_VARIANCE) {
        return 0.5 * (s1 + s2) * (f2 - f1);
    }
    if (s1 <= MIN_NOISE_VARIANCE) {
        return s2 * (f2 - f1) / 2.0;
    }
    if (s2 <= MIN_NOISE_VARIANCE) {
        return s1 * (f2 - f1) / 2.0;
    }
    const double d_ln_s = std::log(s2 / s1);
    const double alpha = d_ln_s / d_ln_f;
    if (std::fabs(alpha + 1.0) < INVERSE_SLOPE_TOLERANCE) {
        return s2 * f2 * d_ln_f;
    }
    return (s2 * f2 - s1 * f1) / (alpha + 1.0);
}

NoiseAccumulator MakeAccumulator(const CompiledCircuit &compiled, const std::size_t point_count) {
    const std::size_t device_count = compiled.Devices.size();
    NoiseAccumulator accumulator;
    accumulator.DeviceIndex.reserve(device_count);
    for (std::size_t d = 0; d < device_count; ++d) {
        accumulator.DeviceIndex[compiled.Devices[d].get()] = d;
    }
    accumulator.PreviousDeviceVariances.assign(device_count, 0.0);
    accumulator.IntegratedDeviceVariances.assign(device_count, 0.0);
    accumulator.GroupVariances.assign(compiled.NoiseGroups.size(), std::vector<double>(point_count, 0.0));
    return accumulator;
}

/**
 * @brief   Checks the output node, the frequency range and the source, in that order, and returns the source.
 */
std::expected<InputSource, std::string> ResolveNoiseInput(const CompiledCircuit &compiled,
                                                          const NoiseConfig &settings) {
    if (const std::optional<std::string> refusal = CheckOutputNode(compiled, settings.OutputPositiveNode)) {
        return std::unexpected(*refusal);
    }
    if (!IsValidSweepRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return std::unexpected(
            "The noise analysis has invalid frequency settings; check start, stop and points per decade");
    }
    return ResolveInputSource(compiled, settings.InputSource, "a noise analysis");
}

/**
 * @brief   The gain from the input source to the output, read from the adjoint solution.
 */
std::complex<double> FindSourceGain(const InputSource &input, const std::vector<std::complex<double>> &adjoint) {
    if (const VoltageSourceDevice *voltage = input.AsVoltage()) {
        return adjoint[static_cast<std::size_t>(voltage->GetBranch())];
    }
    const IndependentSourceDevice &source = input.GetSource();
    const std::complex<double> la =
        source.GetNodeA() < 0 ? std::complex<double>{0.0, 0.0} : adjoint[static_cast<std::size_t>(source.GetNodeA())];
    const std::complex<double> lb =
        source.GetNodeB() < 0 ? std::complex<double>{0.0, 0.0} : adjoint[static_cast<std::size_t>(source.GetNodeB())];
    return lb - la;
}

/**
 * @brief   Takes the output variance of every device at one frequency and adds the step since the previous one to
 *          the integrals.
 * @return  The output variance of all devices at the frequency.
 */
double AddDeviceVariances(NoiseAccumulator &accumulator,
                          const CompiledCircuit &compiled,
                          const std::vector<double> &x_dc,
                          const std::vector<std::complex<double>> &adjoint,
                          const std::vector<double> &frequencies,
                          const std::size_t point,
                          const double inv_gain_sq) {
    const double frequency = frequencies[point];
    double total_out_var = 0.0;
    for (std::size_t d = 0; d < compiled.Devices.size(); ++d) {
        const double out_var = compiled.Devices[d]->OutputNoiseVariance(x_dc, adjoint, frequency);
        total_out_var += out_var;
        if (point > 0) {
            const double step_out =
                IntegrateNoiseStep(frequencies[point - 1], frequency, accumulator.PreviousDeviceVariances[d], out_var);
            const double step_in = step_out * inv_gain_sq;
            accumulator.IntegratedDeviceVariances[d] += step_out;
            accumulator.IntegratedOutputVariance += step_out;
            accumulator.IntegratedInputVariance += step_in;
        }
        accumulator.PreviousDeviceVariances[d] = out_var;
    }
    return total_out_var;
}

void StoreGroupVariances(NoiseAccumulator &accumulator, const CompiledCircuit &compiled, const std::size_t point) {
    for (std::size_t g = 0; g < compiled.NoiseGroups.size(); ++g) {
        double g_var = 0.0;
        for (const Device *dev : compiled.NoiseGroups[g].Devices) {
            const auto it = accumulator.DeviceIndex.find(dev);
            if (it != accumulator.DeviceIndex.end()) {
                g_var += accumulator.PreviousDeviceVariances[it->second];
            }
        }
        accumulator.GroupVariances[g][point] = g_var;
    }
}

/**
 * @brief   The groups that contribute, the largest total first.
 */
std::vector<NoiseContribution> BuildContributions(const CompiledCircuit &compiled,
                                                  const NoiseAccumulator &accumulator,
                                                  const std::size_t point_count) {
    std::vector<NoiseContribution> contributions;
    for (std::size_t g = 0; g < compiled.NoiseGroups.size(); ++g) {
        double group_total_var = 0.0;
        for (const Device *dev : compiled.NoiseGroups[g].Devices) {
            const auto it = accumulator.DeviceIndex.find(dev);
            if (it != accumulator.DeviceIndex.end()) {
                group_total_var += accumulator.IntegratedDeviceVariances[it->second];
            }
        }
        if (group_total_var <= 0.0) {
            continue;
        }
        NoiseContribution contribution;
        contribution.Name = compiled.NoiseGroups[g].Name;
        contribution.OutputDensities.resize(point_count);
        for (std::size_t k = 0; k < point_count; ++k) {
            contribution.OutputDensities[k] = std::sqrt(accumulator.GroupVariances[g][k]);
        }
        contribution.OutputTotal = std::sqrt(group_total_var);
        contributions.push_back(std::move(contribution));
    }
    std::ranges::stable_sort(contributions, std::ranges::greater{}, &NoiseContribution::OutputTotal);
    return contributions;
}

} // namespace

/**
 * @brief   Computes the noise density at an output node and referred to an input source, over a range of
 *          frequencies.
 * @param[in,out] compiled  Compiled circuit with its matrix and devices.
 * @param[in]     settings  Output node, input source and frequency range.
 * @param[in]     options   Numerical limits of the operating point.
 * @param[out]    messages  Diagnostic messages collected during the analysis; may be null.
 * @return  The densities, their integrals over the range and the contribution of each group of devices, or an error
 *          description.
 */
std::expected<NoiseResult, std::string> ComputeNoise(CompiledCircuit &compiled,
                                                     const NoiseConfig &settings,
                                                     const ConvergenceOptions &options,
                                                     std::vector<DiagnosticMessage> *messages) {

    const std::expected<InputSource, std::string> input = ResolveNoiseInput(compiled, settings);
    if (!input) {
        return std::unexpected(input.error());
    }
    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "Nodalys found no operating point to linearize the noise analysis around",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }
    const MnaSystem reactive = BuildReactiveSystem(compiled, *x_dc);

    NoiseResult result;
    result.OutputNode = settings.OutputPositiveNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = input->IsVoltage() ? "V" : "A";
    result.Frequencies = MakeFrequencySweep(settings.SweepType,
                                            settings.StartFrequency,
                                            settings.StopFrequency,
                                            settings.PointsPerDecade);

    const std::size_t point_count = result.Frequencies.size();
    result.OutputDensities.assign(point_count, 0.0);
    result.InputDensities.assign(point_count, 0.0);
    NoiseAccumulator accumulator = MakeAccumulator(compiled, point_count);

    const int out_unknown = compiled.System->NodeIndex(settings.OutputPositiveNode);
    std::vector<std::complex<double>> adjoint(static_cast<std::size_t>(compiled.System->GetSize()));
    for (std::size_t k = 0; k < point_count; ++k) {
        const double frequency = result.Frequencies[k];
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAcTranspose(reactive, omega, out_unknown, adjoint)) {
            return std::unexpected(
                std::format("The Nodalys noise solver encountered a singular matrix at f = {} Hz", frequency));
        }

        const std::complex<double> gain = FindSourceGain(*input, adjoint);
        const double inv_gain_sq = 1.0 / std::max(std::norm(gain), MIN_GAIN_SQUARED);
        const double total_out_var =
            AddDeviceVariances(accumulator, compiled, *x_dc, adjoint, result.Frequencies, k, inv_gain_sq);
        result.OutputDensities[k] = std::sqrt(total_out_var);
        result.InputDensities[k] = std::sqrt(total_out_var * inv_gain_sq);
        StoreGroupVariances(accumulator, compiled, k);
    }

    result.OutputTotal = std::sqrt(accumulator.IntegratedOutputVariance);
    result.InputTotal = std::sqrt(accumulator.IntegratedInputVariance);
    result.Contributions = BuildContributions(compiled, accumulator, point_count);

    if (messages) {
        messages->push_back({std::format("nodalys: noise analysis computed {} frequency points", point_count), false});
    }
    return result;
}

} // namespace Nodalys
