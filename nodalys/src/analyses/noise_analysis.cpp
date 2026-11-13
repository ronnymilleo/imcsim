/**
 * @file    noise_analysis.cpp
 * @brief   Frequency-domain thermal and shot noise spectral density solver.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <unordered_map>

namespace Nodalys {

namespace {

constexpr double MIN_NOISE_VARIANCE = 1e-38;
constexpr double MIN_GAIN_SQUARED = 1e-38;

double IntegrateNoiseStep(const double f1, const double f2, const double s1, const double s2) {
    if (s1 <= 0.0 && s2 <= 0.0) {
        return 0.0;
    }
    const double d_ln_f = std::log(f2 / f1);
    if (std::fabs(d_ln_f) < 1e-12) {
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
    if (std::fabs(alpha + 1.0) < 1e-10) {
        return s2 * f2 * d_ln_f;
    }
    return (s2 * f2 - s1 * f1) / (alpha + 1.0);
}

} // namespace

std::expected<NoiseResult, std::string> ComputeNoise(CompiledCircuit &compiled, const NoiseConfig &settings,
                                                     const ConvergenceOptions &options,
                                                     std::vector<DiagnosticMessage> *messages) {

    const int out_node = settings.OutputPositiveNode;
    if (out_node <= 0 || out_node >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no output node {}", out_node));
    }
    if (settings.StartFrequency <= 0.0 || settings.StopFrequency < settings.StartFrequency ||
        settings.PointsPerDecade <= 0) {
        return std::unexpected(
            "The noise analysis has invalid frequency settings; check start, stop and points per decade");
    }

    const auto source_it = compiled.SourcesByName.find(settings.InputSource);
    if (source_it == compiled.SourcesByName.end()) {
        return std::unexpected(std::format("The circuit has no source named {}", settings.InputSource));
    }

    auto *voltage_source = dynamic_cast<VoltageSourceDevice *>(source_it->second);
    auto *current_source = dynamic_cast<CurrentSourceDevice *>(source_it->second);
    if (voltage_source == nullptr && current_source == nullptr) {
        return std::unexpected(std::format("The source {} cannot drive a noise analysis", settings.InputSource));
    }

    const int size = compiled.System->GetSize();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected("Nodalys found no operating point to linearize the noise analysis around");
    }
    DescribeOutcome(outcome, "operating point", messages);

    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    NoiseResult result;
    result.OutputNode = out_node;
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

    const int out_unknown = compiled.System->NodeIndex(out_node);
    std::vector<std::complex<double>> adjoint(static_cast<std::size_t>(size));

    for (std::size_t k = 0; k < point_count; ++k) {
        const double frequency = result.Frequencies[k];
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAcTranspose(reactive, omega, out_unknown, adjoint)) {
            return std::unexpected(
                std::format("The Nodalys noise solver encountered a singular matrix at f = {} Hz", frequency));
        }

        std::complex<double> gain{0.0, 0.0};
        if (voltage_source != nullptr) {
            gain = adjoint[static_cast<std::size_t>(voltage_source->GetBranch())];
        } else {
            const std::complex<double> la = current_source->GetNodeA() < 0
                                                ? std::complex<double>{0.0, 0.0}
                                                : adjoint[static_cast<std::size_t>(current_source->GetNodeA())];
            const std::complex<double> lb = current_source->GetNodeB() < 0
                                                ? std::complex<double>{0.0, 0.0}
                                                : adjoint[static_cast<std::size_t>(current_source->GetNodeB())];
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

    if (messages) {
        messages->push_back({std::format("nodalys: noise analysis computed {} frequency points", point_count), false});
    }
    return result;
}

} // namespace Nodalys
