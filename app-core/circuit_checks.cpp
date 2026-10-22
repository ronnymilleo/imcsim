/**
 * @file    circuit_checks.cpp
 * @brief   Checks of a circuit and of the settings of an analysis, and the lists of parts an analysis can use.
 */

#include "circuit_checks.h"

#include "components/controlled_source.h"
#include "components/logic_part.h"
#include "components/source.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Core {

namespace {

// Limits that keep a mistyped setting from running for minutes or exhausting memory
constexpr int MAX_TRANSIENT_POINTS = 1000000;
constexpr int MAX_POINTS_PER_DECADE = 1000;
constexpr int MAX_DC_SWEEP_POINTS = 100000;

// Clocks, logic inputs, supply rails and logic parts tie themselves to node 0 in the netlist, so a circuit made only of
// them has its reference without a Ground part
bool IsDigitalOnly(const Circuit &circuit) {
    return std::ranges::all_of(circuit.GetEntries(), [](const CircuitEntry &entry) {
        const ComponentType type = entry.Part->GetType();
        return type == ComponentType::CLOCK || type == ComponentType::LOGIC_INPUT || type == ComponentType::VCC ||
               IsLogicPart(type);
    });
}

std::optional<std::string> CheckFrequencyRange(const double start_frequency, const double stop_frequency,
                                               const int points_per_decade) {
    if (start_frequency <= 0.0) {
        return "The start frequency must be positive";
    }
    if (stop_frequency <= start_frequency) {
        return "The stop frequency must be higher than the start frequency";
    }
    if (points_per_decade < 1 || points_per_decade > MAX_POINTS_PER_DECADE) {
        return std::format("The points per decade must be between 1 and {}", MAX_POINTS_PER_DECADE);
    }
    return std::nullopt;
}

// The sources a DC sweep can drive: independent voltage and current sources, and supply rails
bool IsSweepable(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CURRENT_SOURCE;
}

// Values from start to stop, both included, as ngspice counts them
double SweepPointCount(const SweepRange &range) {
    return std::floor((range.Stop - range.Start) / range.Step + 1e-9) + 1.0;
}

std::optional<std::string> CheckSweepRange(const Circuit &circuit, const SweepRange &range, const char *role) {
    if (range.Source.empty()) {
        return std::format("Pick the {} source", role);
    }
    if (FindSweepableSource(circuit, range.Source) == nullptr) {
        return std::format("The {} source {} is not a voltage source, current source or VCC in the circuit", role,
                           range.Source);
    }
    if (range.Start == range.Stop) {
        return std::format("The start and stop values of the {} source must differ", role);
    }
    if (range.Step == 0.0 || (range.Stop - range.Start) / range.Step < 0.0) {
        return std::format("The step of the {} source must go from its start value toward its stop value", role);
    }
    return std::nullopt;
}

// Independent sources set to AC, which ngspice accepts as the input of a noise analysis
bool IsNoiseInput(const Component &component) {
    const ComponentType type = component.GetType();
    return (type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CURRENT_SOURCE) &&
           static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

// Independent sources a transfer function analysis can drive, whatever their waveform: voltage and current sources
// and supply rails, as a DC sweep can
bool IsTransferFunctionInput(const Component &component) {
    const ComponentType type = component.GetType();
    return type == ComponentType::VCC || type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CURRENT_SOURCE;
}

} // namespace

/**
 * @brief   Checks that a circuit can be simulated at all.
 * @param[in] circuit  Circuit to check.
 * @return  The reason it cannot, in words for the user: it is empty, it has no ground (a circuit of only clocks, logic
 *          inputs, supply rails and logic parts needs none), or a current-controlled source follows no valid current;
 *          no value when it can.
 */
std::optional<std::string> CheckCircuit(const Circuit &circuit) {
    if (circuit.GetEntries().empty()) {
        return "The schematic is empty";
    }
    if (!circuit.HasGround() && !IsDigitalOnly(circuit)) {
        return "The circuit has no ground; add a Ground so node voltages have a reference";
    }
    // A current-controlled source needs a current that some other part reports
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsCurrentControlled(entry.Part->GetType())) {
            continue;
        }
        const std::string &name = entry.Part->GetName();
        const std::string &current = static_cast<const ControlledSource &>(*entry.Part).GetControllingCurrent();
        if (current.empty()) {
            return std::format("{} follows no current; pick its controlling current in Properties", name);
        }
        if (current == name) {
            return std::format("{} cannot follow its own current", name);
        }
        if (!FindControllingSource(circuit, current)) {
            return std::format("{} follows I({}), which is not in the circuit", name, current);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the time span and step of a transient.
 * @param[in] settings  Stop time and time step.
 * @return  The reason they cannot run, such as a step longer than the stop time or too many points; no value when they
 *          can.
 */
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
    if (settings.StopTime / settings.TimeStep > MAX_TRANSIENT_POINTS) {
        return std::format("The time step is too small for the stop time: it would produce more than {} points",
                           MAX_TRANSIENT_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency range of an AC sweep and that the circuit has an AC source to drive it.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Frequency range and points per decade.
 * @return  The reason the sweep cannot run; no value when it can.
 */
std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (!circuit.HasACSource()) {
        return "An AC sweep needs an AC source; select a voltage or current source and set it to AC";
    }
    return std::nullopt;
}

/**
 * @brief   Checks the sources and ranges of a DC sweep, including the optional stepped source.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Swept source and range, and the stepped one.
 * @return  The reason the sweep cannot run, such as a source that is not in the circuit or too many points; no value
 * when it can.
 */
std::optional<std::string> CheckDCSweepSettings(const Circuit &circuit, const DCSweepSettings &settings) {
    if (std::optional<std::string> error = CheckSweepRange(circuit, settings.Swept, "swept")) {
        return error;
    }
    double point_count = SweepPointCount(settings.Swept);
    if (settings.Stepped) {
        if (std::optional<std::string> error = CheckSweepRange(circuit, *settings.Stepped, "stepped")) {
            return error;
        }
        if (settings.Stepped->Source == settings.Swept.Source) {
            return "The stepped source must differ from the swept one";
        }
        point_count *= SweepPointCount(*settings.Stepped);
    }
    if (point_count > MAX_DC_SWEEP_POINTS) {
        return std::format("The steps are too small for the ranges: they would produce more than {} points",
                           MAX_DC_SWEEP_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node, input source and frequency range of a noise analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node, AC input source and frequency range.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckNoiseSettings(const Circuit &circuit, const NoiseSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no noise to measure";
    }
    if (settings.InputSource.empty()) {
        return "A noise analysis needs an AC source as its input; select a voltage or current source and set it to AC";
    }
    if (FindNoiseInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not a voltage or current source set to AC", settings.InputSource);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node and input source of a transfer function analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckTransferFunctionSettings(const Circuit &circuit,
                                                         const TransferFunctionSettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no transfer function";
    }
    if (settings.InputSource.empty()) {
        return "A transfer function analysis needs an input source; select a voltage source, current source or VCC";
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not a voltage source, current source or VCC in the circuit",
                           settings.InputSource);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the inputs and outputs of a truth table.
 * @param[in] circuit   Circuit to run.
 * @param[in] settings  The logic inputs to vary and the nodes to read.
 * @return  The reason the table cannot be built, such as no inputs, more than the limit, a name that is not a logic
 * input or an output node that is ground; no value when it can.
 */
std::optional<std::string> CheckTruthTableSettings(const Circuit &circuit, const TruthTableSettings &settings) {
    if (settings.Inputs.empty()) {
        return "A truth table needs at least one logic input to vary; add a Logic input";
    }
    if (settings.Inputs.size() > MAX_TRUTH_TABLE_INPUTS) {
        return std::format("A truth table varies at most {} inputs, which is {} runs; clear some",
                           MAX_TRUTH_TABLE_INPUTS, 1u << MAX_TRUTH_TABLE_INPUTS);
    }
    for (std::size_t index = 0; index < settings.Inputs.size(); ++index) {
        const std::string &name = settings.Inputs[index];
        if (FindLogicInput(circuit, name) == nullptr) {
            return std::format("The input {} is not a logic input in the circuit", name);
        }
        if (std::ranges::count(settings.Inputs, name) > 1) {
            return std::format("The input {} is listed twice", name);
        }
    }
    if (settings.OutputNodes.empty()) {
        return "Measure at least one node, so the truth table has an output to read";
    }
    for (const int node : settings.OutputNodes) {
        if (node < 1 || node >= circuit.GetNodeCount()) {
            return "Pick output nodes of the circuit; ground has no logic level to read";
        }
    }
    return std::nullopt;
}

/**
 * @brief   Finds a source a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage source, current source or VCC rail of that name.
 */
const Component *FindSweepableSource(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsSweepable(candidate.Part->GetType());
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a source a noise analysis can refer its noise to.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage or current source of that name set to AC.
 */
const Component *FindNoiseInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsNoiseInput(*candidate.Part);
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a source a transfer function analysis can drive.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage source, current source or VCC rail of that name.
 */
const Component *FindTransferFunctionInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsTransferFunctionInput(*candidate.Part);
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a logic input.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no logic input of that name.
 */
const Component *FindLogicInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetType() == ComponentType::LOGIC_INPUT && candidate.Part->GetName() == name;
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Names the unit of a source.
 * @param[in] source  A voltage source, current source or supply rail.
 * @return  "A" for a current source and "V" for the others.
 */
const char *SourceUnit(const Component &source) {
    return source.GetType() == ComponentType::CURRENT_SOURCE ? "A" : "V";
}

/**
 * @brief   Lists the sources a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage sources, current sources and VCC rails, in circuit order.
 */
std::vector<std::string> GetSweepableSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsSweepable(entry.Part->GetType())) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the sources a noise analysis can refer its noise to.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage and current sources set to AC, in circuit order.
 */
std::vector<std::string> GetNoiseInputSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsNoiseInput(*entry.Part)) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the sources a transfer function analysis can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage sources, current sources and VCC rails, of any waveform, in circuit order.
 */
std::vector<std::string> GetTransferFunctionInputSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsTransferFunctionInput(*entry.Part)) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the logic inputs a truth table can vary.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its logic inputs, in circuit order.
 */
std::vector<std::string> GetTruthTableInputs(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetType() == ComponentType::LOGIC_INPUT) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

} // namespace Core
