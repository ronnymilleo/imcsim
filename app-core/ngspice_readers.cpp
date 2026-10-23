/**
 * @file    ngspice_readers.cpp
 * @brief   Reads the results of each analysis from the current plot of ngspice.
 * @details ngGet_Vec_Info fills one static record, so a vector's values are copied before the next one is looked up.
 */

#include "ngspice_readers.h"

#include "circuit_checks.h"
#include "components/source.h"
#include "ngspice_library.h"
#include "ratings.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <format>
#include <ngspice/sharedspice.h>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

namespace Core {

namespace {

// 1e-15 V is -300 dB
constexpr double MIN_MAGNITUDE = 1e-15;

// ngspice keeps no reference to the name after the call
pvector_info FindVector(std::string name) {
    const pvector_info vector = ngGet_Vec_Info(name.data());
    if (vector == nullptr || vector->v_length < 1 || (vector->v_realdata == nullptr && vector->v_compdata == nullptr)) {
        return nullptr;
    }
    return vector;
}

// Analyses with complex results, such as .ac, also store their scale (the frequency) as complex numbers
std::vector<double> RealPart(const vector_info &vector) {
    std::vector<double> values(static_cast<std::size_t>(vector.v_length));
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = vector.v_realdata != nullptr ? vector.v_realdata[index] : vector.v_compdata[index].cx_real;
    }
    return values;
}

std::vector<std::complex<double>> ComplexValues(const vector_info &vector) {
    std::vector<std::complex<double>> values(static_cast<std::size_t>(vector.v_length));
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = vector.v_compdata != nullptr
                            ? std::complex<double>(vector.v_compdata[index].cx_real, vector.v_compdata[index].cx_imag)
                            : std::complex<double>(vector.v_realdata[index], 0.0);
    }
    return values;
}

// ngspice stores vector names in lower case
std::string ToLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::tolower(character); });
    return text;
}

// The branch current of a voltage source, an inductor or a current probe
std::string BranchVectorName(const std::string &element_name) {
    return std::format("{}#branch", ToLower(element_name));
}

/**
 * @struct  CurrentVector
 * @brief   A current the results report, and the ngspice vector it is read from.
 */
struct CurrentVector {
    std::string TraceName;
    std::string VectorName;
};

// Where ngspice saves the currents of a component, with .options savecurrents and the current probes. Two-terminal
// parts report one current, positive from their first terminal to their second; probed parts one per probed
// terminal, positive into it
std::vector<CurrentVector> CurrentVectors(const Component &component) {
    const std::vector<CurrentProbe> probes = GetCurrentProbes(component);
    if (!probes.empty()) {
        std::vector<CurrentVector> vectors;
        for (const CurrentProbe &probe : probes) {
            vectors.push_back({GetCurrentName(component, probe.Label),
                               BranchVectorName(GetCurrentProbeName(component, probe.Label))});
        }
        return vectors;
    }
    const ComponentType type = component.GetType();
    if (!HasSingleCurrent(type)) {
        return {};
    }
    const std::string name = ToLower(component.GetName());
    if (type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR || type == ComponentType::INDUCTOR) {
        return {{component.GetName(), std::format("@{}[i]", name)}};
    }
    if (type == ComponentType::CURRENT_SOURCE) {
        return {{component.GetName(), std::format("@{}[current]", name)}};
    }
    return {{component.GetName(), BranchVectorName(name)}};
}

// 20 log10 of the magnitude, with a floor that keeps a value the AC sources do not reach finite for plotting
double ToDecibels(const std::complex<double> value) {
    return 20.0 * std::log10(std::max(std::abs(value), MIN_MAGNITUDE));
}

double ToDegrees(const std::complex<double> value) {
    return std::arg(value) * 180.0 / std::numbers::pi;
}

// ngspice computes no device currents in .ac, so resistors, capacitors and current sources are worked out from
// their node voltages and values; inductors and voltage sources have their branch current in the results. Probed
// parts are read from their probes instead
std::expected<std::vector<std::complex<double>>, std::string>
ReadACCurrent(const std::string &vector_name, const std::string &trace_name, const std::size_t point_count) {
    const pvector_info vector = FindVector(vector_name);
    if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
        return std::unexpected(std::format("ngspice returned no currents for {}", trace_name));
    }
    return ComplexValues(*vector);
}

void AddACCurrent(std::string name, const std::vector<std::complex<double>> &currents, ACSweep &result) {
    ComponentTrace magnitudes{name, {}};
    ComponentTrace phases{std::move(name), {}};
    std::ranges::transform(currents, std::back_inserter(magnitudes.Values), ToDecibels);
    std::ranges::transform(currents, std::back_inserter(phases.Values), ToDegrees);
    result.CurrentMagnitudesDecibels.push_back(std::move(magnitudes));
    result.CurrentPhasesDegrees.push_back(std::move(phases));
}

// Voltage across the part times its admittance: 1/R for a resistor, j*2*pi*f*C for a capacitor
std::vector<std::complex<double>>
PassiveACCurrent(const CircuitEntry &entry, const std::vector<double> &frequencies,
                 const std::vector<std::vector<std::complex<double>>> &node_voltages) {
    const Component &component = *entry.Part;
    std::vector<std::complex<double>> currents(frequencies.size());
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        const std::complex<double> voltage = node_voltages[static_cast<std::size_t>(entry.Nodes[0])][index] -
                                             node_voltages[static_cast<std::size_t>(entry.Nodes[1])][index];
        const std::complex<double> admittance =
            component.GetType() == ComponentType::RESISTOR
                ? std::complex<double>(1.0 / component.GetValue(), 0.0)
                : std::complex<double>(0.0, 2.0 * std::numbers::pi * frequencies[index] * component.GetValue());
        currents[index] = voltage * admittance;
    }
    return currents;
}

// The source drives its AC magnitude at zero phase, and nothing when it is not AC
std::vector<std::complex<double>> CurrentSourceACCurrent(const Component &component, const std::size_t point_count) {
    const auto &source = static_cast<const Source &>(component);
    const double magnitude = source.GetSourceType() == Source::SourceType::AC ? source.GetAC().Magnitude : 0.0;
    return std::vector<std::complex<double>>(point_count, std::complex<double>(magnitude, 0.0));
}

bool HasACBranchCurrent(const ComponentType type) {
    static constexpr std::array BRANCH_CURRENT_TYPES = {ComponentType::INDUCTOR, ComponentType::VCC,
                                                        ComponentType::VOLTAGE_SOURCE, ComponentType::CLOCK,
                                                        ComponentType::LOGIC_INPUT};
    return std::ranges::contains(BRANCH_CURRENT_TYPES, type);
}

std::expected<std::vector<std::complex<double>>, std::string>
ACCurrent(const CircuitEntry &entry, const std::vector<double> &frequencies,
          const std::vector<std::vector<std::complex<double>>> &node_voltages) {
    const Component &component = *entry.Part;
    const ComponentType type = component.GetType();
    if (type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR) {
        return PassiveACCurrent(entry, frequencies, node_voltages);
    }
    if (type == ComponentType::CURRENT_SOURCE) {
        return CurrentSourceACCurrent(component, frequencies.size());
    }
    if (HasACBranchCurrent(type)) {
        return ReadACCurrent(BranchVectorName(component.GetName()), component.GetName(), frequencies.size());
    }
    return std::unexpected(std::format("{} carries no current", component.GetName()));
}

// ngspice saves the vector names of a plot in lower case; the plot name itself is kept as listed
std::vector<std::string> ListVectors(std::string plot) {
    std::vector<std::string> names;
    char *const *vectors = ngSpice_AllVecs(plot.data());
    for (; vectors != nullptr && *vectors != nullptr; ++vectors) {
        names.emplace_back(*vectors);
    }
    return names;
}

/**
 * @struct  NoisePlots
 * @brief   The two plots a noise analysis leaves: the densities across frequency, and the totals over the range.
 */
struct NoisePlots {
    std::string Densities;
    std::string Totals;
};

// The densities are the plot with a frequency scale; the totals have a single point and no scale
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

// The devices with noise in a plot, from the names of their vectors after prefix. ngspice also saves each noise
// source of a device under the device name and a suffix, such as "onoise_q1_ib" next to "onoise_q1", so a name
// that extends another one by "_" is a source, not a device
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

// A device is an element of the part, or one inside the subcircuit of one, which ngspice names after the instance
// between dots, such as "q.x-u1.q1"
bool IsDeviceOf(const std::string_view device, const std::vector<std::string> &elements) {
    return std::ranges::any_of(elements, [device](const std::string &element) {
        return device == element || device.contains(std::format(".{}.", element));
    });
}

// ngspice describes every vector in the same static record, so the values are copied before the next lookup
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

// The plot a transfer function analysis leaves; its name starts with "tf" and it holds one row
std::optional<std::string> FindTransferFunctionPlot() {
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with("tf")) {
            return name;
        }
    }
    return std::nullopt;
}

// One of the three single-value vectors of a transfer function; ngspice names them after the source and the output
std::expected<double, std::string> ReadTransferFunctionValue(const std::string &name) {
    const pvector_info vector = FindVector(name);
    if (vector == nullptr || vector->v_realdata == nullptr) {
        return std::unexpected(std::format("ngspice returned no {}", name));
    }
    return vector->v_realdata[0];
}

} // namespace

/**
 * @brief   Reads the node voltages and component currents of an operating point.
 * @param[in] circuit  Circuit that was simulated, whose parts say which vectors to look for.
 * @return  The operating point, with rating warnings, or the error when a vector is missing.
 */
std::expected<OperatingPoint, std::string> ReadOperatingPoint(const Circuit &circuit) {
    OperatingPoint result;
    result.NodeVoltages.assign(static_cast<std::size_t>(circuit.GetNodeCount()), 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || vector->v_realdata == nullptr) {
            return std::unexpected(std::format("ngspice returned no voltage for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = vector->v_realdata[0];
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || vector->v_realdata == nullptr) {
                return std::unexpected(std::format("ngspice returned no current for {}", current.TraceName));
            }
            result.Currents.push_back({current.TraceName, vector->v_realdata[0]});
        }
    }
    AddRatingWarnings(circuit, result);
    return result;
}

/**
 * @brief   Reads the node voltages and component currents of a transient over time.
 * @param[in] circuit  Circuit that was simulated.
 * @return  The transient, with rating warnings, or the error when a vector is missing.
 */
std::expected<Transient, std::string> ReadTransient(const Circuit &circuit) {
    const pvector_info time = FindVector("time");
    if (time == nullptr) {
        return std::unexpected("ngspice returned no time points");
    }
    Transient result;
    result.Times = RealPart(*time);
    // time points to the record that the next lookup overwrites, so later vectors are checked against this count
    const std::size_t point_count = result.Times.size();
    result.NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    result.NodeVoltages[0].assign(point_count, 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
            return std::unexpected(std::format("ngspice returned no voltages for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = RealPart(*vector);
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
                return std::unexpected(std::format("ngspice returned no currents for {}", current.TraceName));
            }
            result.Currents.push_back({current.TraceName, RealPart(*vector)});
        }
    }
    AddRatingWarnings(circuit, result);
    return result;
}

/**
 * @brief   Reads the magnitude and phase of every node and current of an AC sweep.
 * @param[in] circuit  Circuit that was simulated.
 * @return  The sweep or the error when a vector is missing.
 */
std::expected<ACSweep, std::string> ReadACSweep(const Circuit &circuit) {
    const pvector_info frequency = FindVector("frequency");
    if (frequency == nullptr) {
        return std::unexpected("ngspice returned no frequency points");
    }
    ACSweep result;
    result.Frequencies = RealPart(*frequency);
    const std::size_t point_count = result.Frequencies.size();

    // Ground stays at zero, which the current of a component connected to it needs
    std::vector<std::vector<std::complex<double>>> node_voltages(static_cast<std::size_t>(circuit.GetNodeCount()));
    node_voltages[0].assign(point_count, 0.0);
    result.NodeMagnitudesDecibels.resize(node_voltages.size());
    result.NodePhasesDegrees.resize(node_voltages.size());
    for (std::size_t node = 1; node < node_voltages.size(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
            return std::unexpected(std::format("ngspice returned no response for node {}", node));
        }
        node_voltages[node] = ComplexValues(*vector);
        std::ranges::transform(node_voltages[node], std::back_inserter(result.NodeMagnitudesDecibels[node]),
                               ToDecibels);
        std::ranges::transform(node_voltages[node], std::back_inserter(result.NodePhasesDegrees[node]), ToDegrees);
    }

    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const std::vector<CurrentVector> vectors = CurrentVectors(*entry.Part);
        if (!GetCurrentProbes(*entry.Part).empty()) {
            for (const CurrentVector &current : vectors) {
                const auto currents = ReadACCurrent(current.VectorName, current.TraceName, point_count);
                if (!currents) {
                    return std::unexpected(currents.error());
                }
                AddACCurrent(current.TraceName, *currents, result);
            }
            continue;
        }
        if (vectors.empty()) {
            continue;
        }
        // A supply rail has one terminal; its other side is ground
        CircuitEntry two_terminal = entry;
        two_terminal.Nodes.resize(2, 0);
        const auto currents = ACCurrent(two_terminal, result.Frequencies, node_voltages);
        if (!currents) {
            return std::unexpected(currents.error());
        }
        AddACCurrent(entry.Part->GetName(), *currents, result);
    }
    return result;
}

/**
 * @brief   Reads the curves of a DC sweep, one per value of the stepped source.
 * @param[in] circuit    Circuit that was simulated.
 * @param[in] settings   Swept and stepped sources the run used.
 * @return  The sweep or the error when a vector is missing.
 */
// The swept values repeat once per value of the stepped source, so the curves are the stretches between repeats
std::expected<DCSweep, std::string> ReadDCSweep(const Circuit &circuit, const DCSweepSettings &settings) {
    const Component &swept = *FindSweepableSource(circuit, settings.Swept.Source);
    const pvector_info scale = FindVector(swept.GetType() == ComponentType::CURRENT_SOURCE ? "i-sweep" : "v-sweep");
    if (scale == nullptr) {
        return std::unexpected("ngspice returned no swept values");
    }
    const std::vector<double> all_values = RealPart(*scale);
    const auto repeat = std::ranges::find(all_values.begin() + 1, all_values.end(), all_values.front());
    const auto curve_length = static_cast<std::size_t>(repeat - all_values.begin());
    if (all_values.size() % curve_length != 0) {
        return std::unexpected("ngspice returned curves of different lengths");
    }

    DCSweep result;
    result.SweptSource = swept.GetName();
    result.SweptUnit = SourceUnit(swept);
    result.SweptValues.assign(all_values.begin(), repeat);
    if (settings.Stepped) {
        result.SteppedSource = settings.Stepped->Source;
        result.SteppedUnit = SourceUnit(*FindSweepableSource(circuit, settings.Stepped->Source));
    }
    const std::size_t curve_count = all_values.size() / curve_length;
    result.Curves.resize(curve_count);
    for (std::size_t curve = 0; curve < curve_count; ++curve) {
        if (settings.Stepped) {
            result.Curves[curve].StepValue =
                settings.Stepped->Start + static_cast<double>(curve) * settings.Stepped->Step;
        }
        result.Curves[curve].NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    }

    // Splits a vector that holds every curve back to back
    const auto split = [&](const std::vector<double> &values, const auto &store) {
        for (std::size_t curve = 0; curve < curve_count; ++curve) {
            const auto begin = values.begin() + static_cast<std::ptrdiff_t>(curve * curve_length);
            store(result.Curves[curve], std::vector<double>(begin, begin + static_cast<std::ptrdiff_t>(curve_length)));
        }
    };
    for (int node = 0; node < circuit.GetNodeCount(); ++node) {
        std::vector<double> voltages(all_values.size(), 0.0);
        if (node > 0) {
            const pvector_info vector = FindVector(std::format("v({})", node));
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != all_values.size()) {
                return std::unexpected(std::format("ngspice returned no voltages for node {}", node));
            }
            voltages = RealPart(*vector);
        }
        split(voltages, [node](DCSweepCurve &curve, std::vector<double> values) {
            curve.NodeVoltages[static_cast<std::size_t>(node)] = std::move(values);
        });
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != all_values.size()) {
                return std::unexpected(std::format("ngspice returned no currents for {}", current.TraceName));
            }
            split(RealPart(*vector), [&current](DCSweepCurve &curve, std::vector<double> values) {
                curve.Currents.push_back({current.TraceName, std::move(values)});
            });
        }
    }
    return result;
}

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

/**
 * @brief   Reads the gain and the two impedances of a transfer function analysis.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node and input source the run used.
 * @return  The transfer function or the error when a vector is missing or is not a number.
 */
std::expected<TransferFunction, std::string> ReadTransferFunction(const Circuit &circuit,
                                                                  const TransferFunctionSettings &settings) {
    const std::optional<std::string> plot = FindTransferFunctionPlot();
    if (!plot) {
        return std::unexpected("ngspice returned no transfer function plot");
    }
    SendCommand(std::format("setplot {}", *plot));
    // The vectors are named after the input source and the output expression, in lower case
    const auto gain = ReadTransferFunctionValue("transfer_function");
    const auto input_impedance =
        ReadTransferFunctionValue(std::format("{}#input_impedance", ToLower(settings.InputSource)));
    const auto output_impedance =
        ReadTransferFunctionValue(std::format("output_impedance_at_v({})", settings.OutputNode));
    if (!gain) {
        return std::unexpected(gain.error());
    }
    if (!input_impedance) {
        return std::unexpected(input_impedance.error());
    }
    if (!output_impedance) {
        return std::unexpected(output_impedance.error());
    }
    TransferFunction result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = SourceUnit(*FindTransferFunctionInput(circuit, settings.InputSource));
    result.Gain = *gain;
    result.InputImpedance = *input_impedance;
    result.OutputImpedance = *output_impedance;
    // An infinite value would format as nan or inf, which reads as a broken number; an open path is instead reported
    // around 1e20 ohm, which stays finite and the views turn into an open circuit
    if (!std::isfinite(result.Gain) || !std::isfinite(result.InputImpedance) ||
        !std::isfinite(result.OutputImpedance)) {
        return std::unexpected("ngspice returned a transfer function that is not a number");
    }
    return result;
}

} // namespace Core
