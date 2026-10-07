/**
 * @file    simulator.cpp
 * @brief   Runs circuits through the ngspice shared library and returns their results.
 * @details ngspice keeps global state, so this module talks to one library instance for the whole process.
 *          Runs are synchronous and must happen on one thread; the callbacks fire on that same thread.
 */

#include "simulator.h"

#include "components/diode.h"
#include "components/source.h"
#include "spice_value.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <format>
#include <iterator>
#include <ngspice/sharedspice.h>
#include <numbers>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace Core {

namespace {

constexpr std::string_view OutputPrefix = "stdout ";
constexpr std::string_view ErrorPrefix = "stderr ";
// Limits that keep a mistyped setting from running for minutes or exhausting memory
constexpr int MaxTransientPoints = 1000000;
constexpr int MaxPointsPerDecade = 1000;
// 1e-15 V is -300 dB
constexpr double MinMagnitude = 1e-15;

/**
 * @struct  NgspiceState
 * @brief   Library-wide state shared with the ngspice callbacks.
 */
struct NgspiceState {
    bool Initialized = false;
    // Set when ngspice asks to quit; the library cannot be used again in this process
    bool Exited = false;
    // Where the callbacks store printed lines; only set while a run is in progress
    std::vector<SimulatorMessage> *Messages = nullptr;
};

NgspiceState &GetState() {
    static NgspiceState state;
    return state;
}

// ngspice prefixes every line with the stream it was printed to
int HandleOutput(char *text, int /*id*/, void * /*user_data*/) {
    std::vector<SimulatorMessage> *messages = GetState().Messages;
    if (messages == nullptr || text == nullptr) {
        return 0;
    }
    std::string_view line(text);
    const bool from_error_stream = line.starts_with(ErrorPrefix);
    if (from_error_stream) {
        line.remove_prefix(ErrorPrefix.size());
    } else if (line.starts_with(OutputPrefix)) {
        line.remove_prefix(OutputPrefix.size());
    }
    if (!line.empty()) {
        messages->push_back({std::string(line), from_error_stream});
    }
    return 0;
}

int HandleStatus(char * /*status*/, int /*id*/, void * /*user_data*/) {
    return 0;
}

int HandleExit(int /*status*/, NG_BOOL /*immediate*/, NG_BOOL /*quit*/, int /*id*/, void * /*user_data*/) {
    GetState().Exited = true;
    return 0;
}

bool EnsureInitialized() {
    NgspiceState &state = GetState();
    if (!state.Initialized) {
        ngSpice_Init(HandleOutput, HandleStatus, HandleExit, nullptr, nullptr, nullptr, nullptr);
        state.Initialized = true;
    }
    return !state.Exited;
}

// ngspice takes non-const strings and keeps no reference to them after the call
int SendCommand(std::string command) {
    return ngSpice_Command(command.data());
}

void LoadCircuit(const std::string &netlist) {
    std::vector<std::string> lines;
    std::istringstream stream(netlist);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(std::move(line));
    }
    std::vector<char *> pointers;
    for (std::string &line : lines) {
        pointers.push_back(line.data());
    }
    pointers.push_back(nullptr);
    ngSpice_Circ(pointers.data());
}

bool HasErrorLine(const std::vector<SimulatorMessage> &messages) {
    for (const SimulatorMessage &message : messages) {
        if (message.FromErrorStream && message.Text.starts_with("Error")) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> CheckCircuit(const Circuit &circuit) {
    if (circuit.GetEntries().empty()) {
        return "The schematic is empty";
    }
    if (!circuit.HasGround()) {
        return "The circuit has no ground; add a Ground so node voltages have a reference";
    }
    return std::nullopt;
}

std::optional<std::string> CheckTransientSettings(const TransientSettings &settings) {
    if (settings.StopTime <= 0.0) {
        return "The stop time must be positive";
    }
    if (settings.TimeStep <= 0.0) {
        return "The time step must be positive";
    }
    if (settings.TimeStep > settings.StopTime) {
        return "The time step must not be longer than the stop time";
    }
    if (settings.StopTime / settings.TimeStep > MaxTransientPoints) {
        return std::format("The time step is too small for the stop time: it would produce more than {} points",
                           MaxTransientPoints);
    }
    return std::nullopt;
}

std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings) {
    if (settings.StartFrequency <= 0.0) {
        return "The start frequency must be positive";
    }
    if (settings.StopFrequency <= settings.StartFrequency) {
        return "The stop frequency must be higher than the start frequency";
    }
    if (settings.PointsPerDecade < 1 || settings.PointsPerDecade > MaxPointsPerDecade) {
        return std::format("The points per decade must be between 1 and {}", MaxPointsPerDecade);
    }
    if (!circuit.HasACSource()) {
        return "An AC sweep needs an AC source; select a voltage or current source and set it to AC";
    }
    return std::nullopt;
}

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
    const std::string name = ToLower(component.GetName());
    switch (component.GetType()) {
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
        return {{component.GetName(), std::format("@{}[i]", name)}};
    case ComponentType::VCC:
    case ComponentType::VoltageSource:
        return {{component.GetName(), BranchVectorName(name)}};
    case ComponentType::CurrentSource:
        return {{component.GetName(), std::format("@{}[current]", name)}};
    case ComponentType::Ground:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        break;
    }
    return {};
}

// 20 log10 of the magnitude, with a floor that keeps a value the AC sources do not reach finite for plotting
double ToDecibels(const std::complex<double> value) {
    return 20.0 * std::log10(std::max(std::abs(value), MinMagnitude));
}

double ToDegrees(const std::complex<double> value) {
    return std::arg(value) * 180.0 / std::numbers::pi;
}

// Zeners work in breakdown, but other diodes only reach it when the circuit exceeds their rating, which ngspice
// simulates without complaint
std::optional<std::string> BreakdownWarning(const CircuitEntry &entry, const double highest_reverse_voltage) {
    if (entry.Part->GetType() == ComponentType::ZenerDiode) {
        return std::nullopt;
    }
    const auto &diode = static_cast<const Diode &>(*entry.Part);
    const double breakdown_voltage = diode.GetParameters().BreakdownVoltage;
    if (highest_reverse_voltage < breakdown_voltage) {
        return std::nullopt;
    }
    return std::format("{} ({}) goes into reverse breakdown: it sees up to {}V in reverse but is rated for {}V",
                       diode.GetName(), diode.GetModelName(), FormatValue(highest_reverse_voltage),
                       FormatValue(breakdown_voltage));
}

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
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
        const double reverse_voltage = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[1])] -
                                       result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[0])];
        if (std::optional<std::string> warning = BreakdownWarning(entry, reverse_voltage)) {
            result.Warnings.push_back(std::move(*warning));
        }
    }
    return result;
}

std::expected<Transient, std::string> ReadTransient(const Circuit &circuit) {
    const pvector_info time = FindVector("time");
    if (time == nullptr) {
        return std::unexpected("ngspice returned no time points");
    }
    Transient result;
    result.Times = RealPart(*time);
    result.NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    result.NodeVoltages[0].assign(result.Times.size(), 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const pvector_info vector = FindVector(std::format("v({})", node));
        if (vector == nullptr || vector->v_length != time->v_length) {
            return std::unexpected(std::format("ngspice returned no voltages for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = RealPart(*vector);
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            const pvector_info vector = FindVector(current.VectorName);
            if (vector == nullptr || vector->v_length != time->v_length) {
                return std::unexpected(std::format("ngspice returned no currents for {}", current.TraceName));
            }
            result.Currents.push_back({current.TraceName, RealPart(*vector)});
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
        const std::vector<double> &anode = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[0])];
        const std::vector<double> &cathode = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[1])];
        double highest_reverse_voltage = cathode[0] - anode[0];
        for (std::size_t index = 1; index < result.Times.size(); ++index) {
            highest_reverse_voltage = std::max(highest_reverse_voltage, cathode[index] - anode[index]);
        }
        if (std::optional<std::string> warning = BreakdownWarning(entry, highest_reverse_voltage)) {
            result.Warnings.push_back(std::move(*warning));
        }
    }
    return result;
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

std::expected<std::vector<std::complex<double>>, std::string>
ACCurrent(const CircuitEntry &entry, const std::vector<double> &frequencies,
          const std::vector<std::vector<std::complex<double>>> &node_voltages) {
    const Component &component = *entry.Part;
    std::vector<std::complex<double>> currents(frequencies.size());
    switch (component.GetType()) {
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
        for (std::size_t index = 0; index < frequencies.size(); ++index) {
            const std::complex<double> voltage = node_voltages[static_cast<std::size_t>(entry.Nodes[0])][index] -
                                                 node_voltages[static_cast<std::size_t>(entry.Nodes[1])][index];
            const std::complex<double> admittance =
                component.GetType() == ComponentType::Resistor
                    ? std::complex<double>(1.0 / component.GetValue(), 0.0)
                    : std::complex<double>(0.0, 2.0 * std::numbers::pi * frequencies[index] * component.GetValue());
            currents[index] = voltage * admittance;
        }
        return currents;
    case ComponentType::CurrentSource: {
        // The source drives its AC magnitude at zero phase, and nothing when it is not AC
        const auto &source = static_cast<const Source &>(component);
        const double magnitude = source.GetSourceType() == Source::SourceType::AC ? source.GetAC().Amplitude : 0.0;
        std::ranges::fill(currents, std::complex<double>(magnitude, 0.0));
        return currents;
    }
    case ComponentType::Inductor:
    case ComponentType::VCC:
    case ComponentType::VoltageSource:
        return ReadACCurrent(BranchVectorName(component.GetName()), component.GetName(), frequencies.size());
    case ComponentType::Ground:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        break;
    }
    return std::unexpected(std::format("{} carries no current", component.GetName()));
}

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

// Runs one analysis on a clean library. Return codes stay 0 even when the netlist is rejected, so success is
// told by the plot the run created, whose name starts with the analysis (op1, tran1, ac1)
template <typename Data, typename Reader>
SimulationRun<Data> Simulate(const Circuit &circuit, const std::string_view analysis,
                             const std::string_view plot_prefix, Reader read_result) {
    SimulationRun<Data> run;
    if (!EnsureInitialized()) {
        run.Result = std::unexpected("ngspice stopped after a fatal error; restart imcsim to simulate again");
        return run;
    }

    NgspiceState &state = GetState();
    state.Messages = &run.Messages;
    // savecurrents makes ngspice keep the current through every component, not only through voltage sources;
    // diodes get probes instead, since ngspice saves no usable diode current in .ac
    LoadCircuit(circuit.ToSpiceNetlist(std::format(".options savecurrents\n{}", analysis), true));
    SendCommand("run");
    const char *plot = ngSpice_CurPlot();
    if (plot == nullptr || !std::string_view(plot).starts_with(plot_prefix) || HasErrorLine(run.Messages)) {
        run.Result = std::unexpected("ngspice could not simulate the circuit; see its output for details");
    } else {
        run.Result = read_result();
    }
    // Drop the results and the circuit, so every run starts from a clean library
    SendCommand("destroy all");
    SendCommand("remcirc");
    state.Messages = nullptr;
    return run;
}

} // namespace

/**
 * @brief   Computes the DC operating point of a circuit with ngspice.
 * @param[in] circuit  Circuit to simulate; it needs a ground component.
 * @return  The node voltages or an error, plus every line ngspice printed. Warnings such as "singular matrix"
 *          can come with a successful result whose values are meaningless, so show the messages to the user.
 * @note    Not thread-safe: ngspice is a single global library instance.
 */
OperatingPointRun RunOperatingPoint(const Circuit &circuit) {
    if (const std::optional<std::string> error = CheckCircuit(circuit)) {
        return {std::unexpected(*error), {}};
    }
    return Simulate<OperatingPoint>(circuit, ".op", "op", [&circuit] { return ReadOperatingPoint(circuit); });
}

/**
 * @brief   Simulates how the node voltages of a circuit change over time, starting from its operating point.
 * @param[in] circuit   Circuit to simulate; it needs a ground component.
 * @param[in] settings  Stop time and time step.
 * @return  The voltages at every time point or an error, plus every line ngspice printed. Invalid settings
 *          are rejected before reaching ngspice.
 * @note    Not thread-safe: ngspice is a single global library instance.
 */
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings) {
    std::optional<std::string> error = CheckCircuit(circuit);
    if (!error) {
        error = CheckTransientSettings(settings);
    }
    if (error) {
        return {std::unexpected(*error), {}};
    }
    const std::string analysis =
        std::format(".tran {} {}", FormatSpiceValue(settings.TimeStep), FormatSpiceValue(settings.StopTime));
    return Simulate<Transient>(circuit, analysis, "tran", [&circuit] { return ReadTransient(circuit); });
}

/**
 * @brief   Sweeps the small-signal response of a circuit across frequency, excited by its AC sources.
 * @param[in] circuit   Circuit to simulate; it needs a ground component and at least one AC source.
 * @param[in] settings  Frequency range and points per decade.
 * @return  The magnitude and phase of every node at every frequency or an error, plus every line ngspice
 *          printed. Invalid settings and circuits without an AC source are rejected before reaching ngspice.
 * @note    Not thread-safe: ngspice is a single global library instance.
 */
ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings) {
    std::optional<std::string> error = CheckCircuit(circuit);
    if (!error) {
        error = CheckACSweepSettings(circuit, settings);
    }
    if (error) {
        return {std::unexpected(*error), {}};
    }
    const std::string analysis =
        std::format(".ac dec {} {} {}", settings.PointsPerDecade, FormatSpiceValue(settings.StartFrequency),
                    FormatSpiceValue(settings.StopFrequency));
    return Simulate<ACSweep>(circuit, analysis, "ac", [&circuit] { return ReadACSweep(circuit); });
}

} // namespace Core
