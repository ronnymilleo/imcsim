/**
 * @file    ngspice_read_noise.cpp
 * @brief   Reads the result of a noise analysis from the two plots ngspice leaves.
 */

#include "backends/ngspice/ngspice_read_noise.h"

#include "backends/ngspice/ngspice_library.h"
#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "circuit/circuit_netlist.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include "simulation/simulation_results.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

namespace {

/**
 * @struct  NoisePlots
 * @brief   The two plots a noise analysis leaves: the densities across frequency, and the totals over the range.
 */
struct NoisePlots {
    std::string Densities;
    std::string Totals;
};

/**
 * @brief   The densities are the plot with a frequency scale; the totals have a single point and no scale.
 */
std::optional<NoisePlots> FindNoisePlots() {
    NoisePlots plots;
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (!name.starts_with("noise")) {
            continue;
        }
        const std::vector<std::string> vectors = ListVectors(name);
        (std::ranges::contains(vectors, "frequency") ? plots.Densities : plots.Totals) = name;
    }
    if (plots.Densities.empty() || plots.Totals.empty()) {
        return std::nullopt;
    }
    return plots;
}

/**
 * @brief   The devices with noise in a plot, from the names of their vectors after prefix. ngspice also saves each
 *          noise source of a device under the device name and a suffix, such as "onoise_q1_ib" next to "onoise_q1", so
 *          a name that extends another one by "_" is a source, not a device.
 */
std::vector<std::string> ListNoiseDevices(const std::vector<std::string> &vectors, const std::string_view prefix) {
    std::vector<std::string> names;
    for (const std::string &vector : vectors) {
        if (vector.starts_with(prefix) && vector.size() > prefix.size()) {
            names.push_back(vector.substr(prefix.size()));
        }
    }
    std::vector<std::string> devices;
    for (const std::string &name : names) {
        const bool is_source = std::ranges::any_of(names, [&name](const std::string &other) {
            return name.size() > other.size() + 1 && name.starts_with(other) && name[other.size()] == '_';
        });
        if (!is_source && name != "spectrum") {
            devices.push_back(name);
        }
    }
    return devices;
}

/**
 * @brief   A device is an element of the part, or one inside the subcircuit of one, which ngspice names after the
 *          instance between dots, such as "q.x-u1.q1".
 */
bool IsDeviceOf(const std::string_view device, const std::vector<std::string> &elements) {
    return std::ranges::any_of(elements, [device](const std::string &element) {
        return device == element || device.contains(std::format(".{}.", element));
    });
}

/**
 * @brief   ngspice describes every vector in the same static record, so the values are copied before the next lookup.
 */
std::optional<std::vector<double>> ReadRealVector(const std::string &name) {
    const pvector_info vector = FindVector(name);
    if (vector == nullptr) {
        return std::nullopt;
    }
    return RealPart(*vector);
}

std::expected<double, std::string> ReadNoiseTotal(const std::string &name) {
    const pvector_info vector = FindVector(name);
    if (vector == nullptr || vector->v_realdata == nullptr) {
        return std::unexpected(std::format("ngspice returned no {}", name));
    }
    return vector->v_realdata[0];
}

// Each part adds up the power of its devices, at every frequency and in total; parts whose noise does not reach the
// output are left out
std::expected<std::vector<NoiseContribution>, std::string>
ReadNoiseContributions(const Circuit &circuit, const NoisePlots &plots, const std::size_t point_count) {
    SendCommand(std::format("setplot {}", plots.Totals));
    const std::vector<std::string> total_devices = ListNoiseDevices(ListVectors(plots.Totals), "onoise_total_");
    std::vector<double> device_totals;
    for (const std::string &device : total_devices) {
        const auto total = ReadNoiseTotal(std::format("onoise_total_{}", device));
        if (!total) {
            return std::unexpected(total.error());
        }
        device_totals.push_back(*total);
    }
    SendCommand(std::format("setplot {}", plots.Densities));
    std::vector<NoiseContribution> contributions;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        std::vector<std::string> elements = GetElementNames(*entry.Part);
        std::ranges::transform(elements, elements.begin(), ToLower);
        NoiseContribution contribution{entry.Part->GetName(), std::vector<double>(point_count, 0.0), 0.0};
        bool has_noise = false;
        for (std::size_t index = 0; index < total_devices.size(); ++index) {
            const std::string &device = total_devices[index];
            if (!IsDeviceOf(device, elements)) {
                continue;
            }
            const std::optional<std::vector<double>> densities = ReadRealVector(std::format("onoise_{}", device));
            if (!densities || densities->size() != point_count) {
                return std::unexpected(std::format("ngspice returned no noise for {}", device));
            }
            for (std::size_t point = 0; point < point_count; ++point) {
                contribution.OutputDensities[point] += (*densities)[point] * (*densities)[point];
            }
            contribution.OutputTotal += device_totals[index] * device_totals[index];
            has_noise = true;
        }
        // A part whose noise does not reach the output, as behind an ideal source, has no share
        if (!has_noise || contribution.OutputTotal <= 0.0) {
            continue;
        }
        std::ranges::transform(contribution.OutputDensities, contribution.OutputDensities.begin(),
                               [](const double power) { return std::sqrt(power); });
        contribution.OutputTotal = std::sqrt(contribution.OutputTotal);
        contributions.push_back(std::move(contribution));
    }
    std::ranges::stable_sort(contributions, std::ranges::greater{}, &NoiseContribution::OutputTotal);
    return contributions;
}

} // namespace

/**
 * @brief   Reads the noise densities, totals and the share of each part from the two plots of a noise analysis.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node and input source the run used.
 * @return  The noise or the error when a plot or vector is missing.
 */
std::expected<Noise, std::string> ReadNoise(const Circuit &circuit, const NoiseSettings &settings) {
    const std::optional<NoisePlots> plots = FindNoisePlots();
    if (!plots) {
        return std::unexpected("ngspice returned no noise plots");
    }
    Noise result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = SourceUnit(*FindNoiseInput(circuit, settings.InputSource));

    SendCommand(std::format("setplot {}", plots->Totals));
    const auto output_total = ReadNoiseTotal("onoise_total");
    const auto input_total = ReadNoiseTotal("inoise_total");
    if (!output_total || !input_total) {
        return std::unexpected(!output_total ? output_total.error() : input_total.error());
    }
    result.OutputTotal = *output_total;
    result.InputTotal = *input_total;

    SendCommand(std::format("setplot {}", plots->Densities));
    const std::optional<std::vector<double>> frequencies = ReadRealVector("frequency");
    const std::optional<std::vector<double>> output = ReadRealVector("onoise_spectrum");
    const std::optional<std::vector<double>> input = ReadRealVector("inoise_spectrum");
    if (!frequencies || !output || !input || output->size() != frequencies->size() ||
        input->size() != frequencies->size()) {
        return std::unexpected("ngspice returned no noise densities");
    }
    result.Frequencies = *frequencies;
    result.OutputDensities = *output;
    result.InputDensities = *input;

    auto contributions = ReadNoiseContributions(circuit, *plots, result.Frequencies.size());
    if (!contributions) {
        return std::unexpected(contributions.error());
    }
    result.Contributions = std::move(*contributions);
    return result;
}

} // namespace Core
