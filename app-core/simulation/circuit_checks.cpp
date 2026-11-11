/**
 * @file    circuit_checks.cpp
 * @brief   Checks of a circuit and of the settings of an analysis, and the lists of parts an analysis can use.
 */

#include "simulation/circuit_checks.h"

#include "components/behavioral_source.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/logic_part.h"
#include "components/source.h"
#include "components/subcircuit.h"
#include "model/subcircuit.h"
#include "spice/behavioral_expression.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string_view>

namespace Core {

namespace {

/// Most points per decade a sweep may ask for, so a mistyped setting cannot run for minutes or exhaust memory.
constexpr int MAX_POINTS_PER_DECADE = 1000;
/// Most points a DC sweep may take, for the same reason.
constexpr int MAX_DC_SWEEP_POINTS = 100000;

/**
 * @brief   Clocks, logic inputs, supply rails and logic parts tie themselves to node 0 in the netlist, so a circuit
 *          made only of them has its reference without a Ground part, and so does one whose blocks hold only them.
 */
bool IsDigitalOnly(const Circuit &circuit) {
    return std::ranges::all_of(CollectParts(circuit), [](const Component *part) {
        const ComponentType type = part->GetType();
        return type == ComponentType::CLOCK || type == ComponentType::LOGIC_INPUT || type == ComponentType::VCC ||
               type == ComponentType::SUBCIRCUIT || IsLogicPart(type);
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

/// Fewest points a linear S-parameter sweep may take: ngspice gives a single point, at the start, for two.
constexpr int MIN_LINEAR_S_POINTS = 3;
/// Most points a linear S-parameter sweep may take.
constexpr int MAX_LINEAR_S_POINTS = 100000;

std::optional<std::string> CheckSParameterRange(const SParameterSettings &settings) {
    if (!settings.IsLogarithmic) {
        if (settings.StartFrequency <= 0.0) {
            return "The start frequency must be positive";
        }
        if (settings.StopFrequency <= settings.StartFrequency) {
            return "The stop frequency must be higher than the start frequency";
        }
        if (settings.LinearPoints < MIN_LINEAR_S_POINTS || settings.LinearPoints > MAX_LINEAR_S_POINTS) {
            return std::format("The number of points must be between {} and {}; ngspice gives only the first point "
                               "for two",
                               MIN_LINEAR_S_POINTS, MAX_LINEAR_S_POINTS);
        }
        return std::nullopt;
    }
    return CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade);
}

/**
 * @brief   Ports are numbered from 1 with no gap and no repeat, which ngspice requires and reports as a fatal error,
 *          while ports on one node it takes without a word.
 */
std::optional<std::string> CheckSParameterPortNumbers(const std::vector<CircuitPort> &ports) {
    std::set<int> numbers;
    for (const CircuitPort &port : ports) {
        if (!numbers.insert(port.Number).second) {
            return std::format("Two S-parameter ports are numbered {}; give each port its own number", port.Number);
        }
    }
    for (int number = 1; number <= static_cast<int>(ports.size()); ++number) {
        if (!numbers.contains(number)) {
            return std::format("The S-parameter ports must be numbered 1 to {} with no gap; port {} is missing",
                               ports.size(), number);
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckSParameterPortNodes(const std::vector<CircuitPort> &ports) {
    for (std::size_t first = 0; first < ports.size(); ++first) {
        const CircuitPort &port = ports[first];
        if (port.ReferenceOhms <= 0.0) {
            return std::format("The reference impedance of port {} must be positive", port.Number);
        }
        if (port.Node == 0) {
            return std::format("Port {} is on ground; an S-parameter port needs a node of its own", port.Number);
        }
        for (std::size_t second = first + 1; second < ports.size(); ++second) {
            if (ports[second].Node == port.Node) {
                return std::format("Ports {} and {} are on the same node; each port needs a node of its own",
                                   port.Number, ports[second].Number);
            }
        }
    }
    return std::nullopt;
}

/**
 * @brief   The independent sources a DC sweep or a transfer function can drive, whatever their waveform: voltage and
 *          current sources, supply rails, and clocks and logic inputs, which are voltage sources in the netlist whose
 *          DC value the analysis replaces.
 */
bool IsSweepable(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VOLTAGE_SOURCE ||
           type == ComponentType::CURRENT_SOURCE || type == ComponentType::CLOCK || type == ComponentType::LOGIC_INPUT;
}

/**
 * @brief   Values from start to stop, both included, as ngspice counts them.
 */
double SweepPointCount(const SweepRange &range) {
    return std::floor((range.Stop - range.Start) / range.Step + 1e-9) + 1.0;
}

std::optional<std::string> CheckSweepRange(const Circuit &circuit, const SweepRange &range, const char *role) {
    if (range.Source.empty()) {
        return std::format("Pick the {} source", role);
    }
    if (FindSweepableSource(circuit, range.Source) == nullptr) {
        return std::format("The {} source {} is not {} of the circuit", role, range.Source, SWEEPABLE_SOURCES);
    }
    if (range.Start == range.Stop) {
        return std::format("The start and stop values of the {} source must differ", role);
    }
    if (range.Step == 0.0 || (range.Stop - range.Start) / range.Step < 0.0) {
        return std::format("The step of the {} source must go from its start value toward its stop value", role);
    }
    return std::nullopt;
}

/**
 * @brief   Independent sources set to AC, which ngspice accepts as the input of a noise analysis.
 */
bool IsNoiseInput(const Component &component) {
    const ComponentType type = component.GetType();
    return (type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CURRENT_SOURCE) &&
           static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

/**
 * @brief   A pin on ground would connect nothing, since ngspice keeps node 0 global instead of joining it to the
 *          outside, and two pins on one node would short the nets of the instance.
 */
std::optional<std::string> CheckPins(const SubcircuitDefinition &definition) {
    // The ports of a block kept as SPICE text are the vendor's own names, written as they came
    if (definition.Spice) {
        if (definition.Spice->Ports.size() != definition.Pins.size()) {
            return std::format("{} has {} pins but its SPICE text has {} ports", definition.Name,
                               definition.Pins.size(), definition.Spice->Ports.size());
        }
        return std::nullopt;
    }
    std::set<int> nodes;
    for (const SubcircuitPin &pin : definition.Pins) {
        if (pin.Node == 0) {
            return std::format("Pin {} of {} sits on ground inside it, so it would connect nothing; ground is shared "
                               "by the whole circuit, so remove the pin",
                               pin.Name, definition.Name);
        }
        if (!nodes.insert(pin.Node).second) {
            return std::format("Pins of {} share a node inside it; give each pin a node of its own", definition.Name);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Only parts inside a subcircuit can follow its parameters.
 */
std::optional<std::string> CheckExpressions(const Circuit &body, const SubcircuitDefinition *definition) {
    for (const CircuitEntry &entry : body.GetEntries()) {
        const std::optional<std::string> &expression = entry.Part->GetValueExpression();
        if (!expression) {
            continue;
        }
        const std::string_view parameter = SubcircuitInstance::GetExpressionParameter(*expression);
        if (definition == nullptr) {
            return std::format("{} follows a parameter, which only a part inside a subcircuit can do",
                               entry.Part->GetName());
        }
        if (!std::ranges::contains(definition->Parameters, parameter, &SubcircuitParameter::Name)) {
            return std::format("{} in {} follows {}, which is not a parameter of it", entry.Part->GetName(),
                               definition->Name, *expression);
        }
    }
    return std::nullopt;
}

/**
 * @brief   The instances of one circuit or block body: each needs a known definition with a node for each pin and
 *          parameters it has, and each definition pins it can connect and parameters for its parts; the blocks inside
 *          a definition are checked the same way, once each.
 * @details open holds the blocks being looked into on the way down, so one that places itself, directly or through
 *          another, is caught instead of looped through.
 */
std::optional<std::string> CheckInstances(const Circuit &circuit, const Circuit &body,
                                          std::set<std::string, std::less<>> &checked,
                                          std::set<std::string, std::less<>> &open) {
    for (const CircuitEntry &entry : body.GetEntries()) {
        if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
            continue;
        }
        const auto &instance = static_cast<const SubcircuitInstance &>(*entry.Part);
        const auto definition = circuit.FindSubcircuit(instance.GetDefinitionName());
        if (!definition) {
            return std::format("{} refers to the unknown subcircuit {}", entry.Part->GetName(),
                               instance.GetDefinitionName());
        }
        if (entry.Nodes.size() != definition->Pins.size()) {
            return std::format("{} does not connect every pin of {}", entry.Part->GetName(), definition->Name);
        }
        for (const auto &[name, value] : instance.GetParameters()) {
            if (!std::ranges::contains(definition->Parameters, name, &SubcircuitParameter::Name)) {
                return std::format("{} sets {}, which is not a parameter of {}", entry.Part->GetName(), name,
                                   definition->Name);
            }
        }
        if (open.contains(definition->Name)) {
            return std::format("{} contains itself, directly or through another block", definition->Name);
        }
        if (!checked.insert(definition->Name).second) {
            continue;
        }
        if (std::optional<std::string> problem = CheckPins(*definition)) {
            return problem;
        }
        if (std::optional<std::string> problem = CheckExpressions(definition->Body, definition.get())) {
            return problem;
        }
        open.insert(definition->Name);
        if (std::optional<std::string> problem = CheckInstances(circuit, definition->Body, checked, open)) {
            return problem;
        }
        open.erase(definition->Name);
    }
    return std::nullopt;
}

/**
 * @brief   Every instance needs a known definition with a node for each pin and parameters it has, and every definition
 *          in use needs pins it can connect and parameters for its parts, down to the blocks inside blocks; no block
 *          may contain itself.
 */
std::optional<std::string> CheckSubcircuits(const Circuit &circuit) {
    if (std::optional<std::string> problem = CheckExpressions(circuit, nullptr)) {
        return problem;
    }
    std::set<std::string, std::less<>> checked;
    std::set<std::string, std::less<>> open;
    return CheckInstances(circuit, circuit, checked, open);
}

/**
 * @brief   A part that names a library model needs the circuit to have it, and of its own kind.
 */
std::optional<std::string> CheckLibraryModels(const Circuit &circuit) {
    for (const Component *part : CollectParts(circuit)) {
        if (!part->HasLibraryModel()) {
            continue;
        }
        const LibraryModel *model = circuit.FindLibraryModel(part->GetLibraryModel());
        if (model == nullptr) {
            return std::format(
                "{} uses the model {}, which the schematic does not have; import it again or pick another "
                "model",
                part->GetName(), part->GetLibraryModel());
        }
        if (!CanUseLibraryModel(part->GetType(), model->Kind)) {
            return std::format("{} uses the model {}, which is a {} model", part->GetName(), model->Name,
                               GetModelKindName(model->Kind));
        }
    }
    return std::nullopt;
}

/**
 * @brief   A noise or random source with a value ngspice cannot take is stopped here: a negative noise time step makes
 *          ngspice loop forever, and it runs inside this process.
 */
std::optional<std::string> CheckSourceWaveforms(const Circuit &circuit) {
    for (const Component *part : CollectParts(circuit)) {
        if (!IsSource(part->GetType())) {
            continue;
        }
        const auto &source = static_cast<const Source &>(*part);
        if (source.GetSourceType() == Source::SourceType::NOISE && !source.IsValidNoise(source.GetNoise())) {
            return std::format("The noise of {} has a value ngspice cannot use: check that the time step is positive "
                               "with white or 1/f noise, the 1/f exponent is between 0 and 2 and the telegraph times "
                               "are positive",
                               part->GetName());
        }
        if (source.GetSourceType() == Source::SourceType::RANDOM && !source.IsValidRandom(source.GetRandom())) {
            return std::format("The random values of {} have a value ngspice cannot use: check that the time step is "
                               "positive and the first parameter is not negative",
                               part->GetName());
        }
    }
    return std::nullopt;
}

/**
 * @brief   The first problem of the behavioral sources of one circuit, a block body counting as a circuit of its own.
 */
std::optional<std::string> CheckBehavioralSources(const Circuit &circuit) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsBehavioralSource(entry.Part->GetType())) {
            if (std::optional<std::string> problem = CheckBehavioralExpression(circuit, *entry.Part)) {
                return problem;
            }
        }
    }
    return std::nullopt;
}

bool IsPowerOfTwo(const int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::optional<std::string> CheckSourceHarmonicRatio(const std::string_view name, const double source_frequency,
                                                    const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(source_frequency) || source_frequency <= 0.0) {
        return std::format("The frequency of {} must be positive for harmonic balance", name);
    }
    const std::optional<int> harmonic = FindHarmonicNumber(source_frequency, settings.FundamentalFrequency);
    if (!harmonic) {
        return std::format("{} runs at {} Hz, which is not an integer multiple of the fundamental {} Hz", name,
                           source_frequency, settings.FundamentalFrequency);
    }
    if (*harmonic > settings.Harmonics) {
        return std::format("{} runs at {} Hz (harmonic {}), which exceeds the {} configured harmonics", name,
                           source_frequency, *harmonic, settings.Harmonics);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks that a circuit can be simulated at all.
 * @param[in] circuit  Circuit to check.
 * @return  The reason it cannot, in words for the user: it is empty, it has no ground (a circuit of only clocks, logic
 *          inputs, supply rails and logic parts, in blocks or not, needs none), a subcircuit instance or definition
 * cannot be written (an unknown definition, a pin on ground, a parameter it lacks), a part uses a library model the
 * circuit lacks or of another kind, or a current-controlled source follows no valid current; no value when it can.
 */
std::optional<std::string> CheckCircuit(const Circuit &circuit) {
    if (circuit.GetEntries().empty()) {
        return "The schematic is empty";
    }
    if (!circuit.HasGround() && !IsDigitalOnly(circuit)) {
        return "The circuit has no ground; add a Ground so node voltages have a reference";
    }
    if (std::optional<std::string> problem = CheckSubcircuits(circuit)) {
        return problem;
    }
    if (std::optional<std::string> problem = CheckLibraryModels(circuit)) {
        return problem;
    }
    if (std::optional<std::string> problem = CheckSourceWaveforms(circuit)) {
        return problem;
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
    if (std::optional<std::string> problem = CheckBehavioralSources(circuit)) {
        return problem;
    }
    for (const auto &definition : circuit.GetSubcircuits()) {
        if (std::optional<std::string> problem = CheckBehavioralSources(definition->Body)) {
            return std::format("In block {}: {}", definition->Name, *problem);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the expression of a behavioral source against its circuit: what ParseBehavioralExpression() checks,
 *          and that every net it names has a label and every current it reads is in the circuit.
 * @param[in] circuit  Circuit the source belongs to, a block body for a source inside a block.
 * @param[in] source   A behavioral source.
 * @return  What is wrong, named after the source, such as "B1: the circuit has no net named OUT"; no value when the
 *          expression can be written for ngspice, whose own errors still show when it runs.
 */
std::optional<std::string> CheckBehavioralExpression(const Circuit &circuit, const Component &source) {
    const std::string &expression = static_cast<const BehavioralSource &>(source).GetExpression();
    const std::string &name = source.GetName();
    if (std::ranges::contains(ListBehavioralCurrents(expression), name)) {
        return std::format("{} cannot read its own current", name);
    }
    const auto written = TranslateBehavioralExpression(
        expression, [&circuit](const std::string_view net) { return circuit.FindNodeOfNet(net); },
        [&circuit](const std::string_view current) { return FindControllingSource(circuit, current); });
    if (!written) {
        return std::format("{}: {}", name, written.error());
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
 * @brief   Finds the shortest interval at which a noise or random source of the circuit changes.
 * @param[in] circuit  Circuit to look through, blocks included.
 * @return  The shortest time step of its noise and random sources; no value when none has one.
 */
std::optional<double> FindFinestSignalStep(const Circuit &circuit) {
    std::optional<double> finest;
    for (const Component *part : CollectParts(circuit)) {
        if (!IsSource(part->GetType())) {
            continue;
        }
        const std::optional<double> step = static_cast<const Source &>(*part).GetSignalStep();
        if (step && (!finest || *step < *finest)) {
            finest = step;
        }
    }
    return finest;
}

/**
 * @brief   Checks that the noise and random sources of a circuit keep a transient within the points it may span.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Stop time and time step.
 * @return  The reason it would span too many points, since ngspice stops at every sample of a noise or random source
 *          and not only at the time step; no value when it does not.
 */
std::optional<std::string> CheckSourceSteps(const Circuit &circuit, const TransientSettings &settings) {
    const std::optional<double> finest = FindFinestSignalStep(circuit);
    if (finest && *finest > 0.0 && settings.StopTime / *finest > MAX_TRANSIENT_POINTS) {
        return std::format("The time step of a noise or random source is too small for the stop time: it would "
                           "produce more than {} points",
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
        return std::format("A transfer function analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node and the input source of a pole-zero analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckPoleZeroSettings(const Circuit &circuit, const PoleZeroSettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no poles or zeros";
    }
    if (settings.InputSource.empty()) {
        return std::format("A pole-zero analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency range of an S-parameter analysis and its ports.
 * @param[in] circuit   Circuit to analyze; its ports are the pin markers set as ports.
 * @param[in] settings  Frequency range and points.
 * @return  The reason the analysis cannot run, such as fewer than two ports or ports numbered with a gap; no value
 *          when it can.
 * @note    ngspice stops for good when it finds fewer than two ports, so this must run before it.
 */
std::optional<std::string> CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings) {
    if (std::optional<std::string> error = CheckSParameterRange(settings)) {
        return error;
    }
    const std::vector<CircuitPort> &ports = circuit.GetPorts();
    if (ports.size() < MIN_S_PARAMETER_PORTS) {
        return "ngspice needs at least two S-parameter ports; turn on S-parameter port in the properties of two or "
               "more "
               "Block pin markers";
    }
    if (std::optional<std::string> error = CheckSParameterPortNumbers(ports)) {
        return error;
    }
    return CheckSParameterPortNodes(ports);
}

/**
 * @brief   Checks the output node of a sensitivity analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node.
 * @return  The reason the analysis cannot run; no value when it can.
 * @note    ngspice answers a node that does not exist with zeros and no error, so the node is checked here.
 */
std::optional<std::string> CheckSensitivitySettings(const Circuit &circuit, const SensitivitySettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no sensitivity";
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
 * @return  The part, or null when the circuit has no voltage source, current source, VCC rail, clock or
 *          logic input of that name.
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
 * @return  The part, or null when the circuit has no voltage source, current source, VCC rail, clock or
 *          logic input of that name.
 */
const Component *FindTransferFunctionInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsSweepable(candidate.Part->GetType());
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
 * @param[in] source  A voltage source, current source, supply rail, clock or logic input.
 * @return  "A" for a current source and "V" for the others.
 */
const char *SourceUnit(const Component &source) {
    return source.GetType() == ComponentType::CURRENT_SOURCE ? "A" : "V";
}

/**
 * @brief   Lists the sources a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage sources, current sources, VCC rails, clocks and logic inputs, in circuit order.
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
 * @return  Names of its voltage sources, current sources, VCC rails, clocks and logic inputs, of any waveform, in
 *          circuit order.
 */
std::vector<std::string> GetTransferFunctionInputSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsSweepable(entry.Part->GetType())) {
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

/**
 * @brief   Checks the fundamental frequency, harmonic count, sample grid, and periodic sources for harmonic balance.
 * @param[in] circuit   Circuit to simulate, including any sources inside blocks.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason harmonic balance cannot run on those settings, or no value when every check passes.
 */
std::optional<std::string> CheckHarmonicBalanceSettings(const Circuit &circuit,
                                                        const HarmonicBalanceSettings &settings) {
    if (std::optional<std::string> error = CheckHarmonicBalanceGrid(settings)) {
        return error;
    }
    for (const Component *part : CollectParts(circuit)) {
        if (part->GetType() == ComponentType::CLOCK) {
            const double frequency = static_cast<const Clock &>(*part).GetParameters().Frequency;
            if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                return error;
            }
            continue;
        }
        if (!IsSource(part->GetType())) {
            continue;
        }
        const auto &source = static_cast<const Source &>(*part);
        switch (source.GetSourceType()) {
        case Source::SourceType::DC:
            break;
        case Source::SourceType::AC: {
            const ACParameters &ac = source.GetAC();
            if (ac.Amplitude != 0.0) {
                if (std::optional<std::string> error =
                        CheckSourceHarmonicRatio(part->GetName(), ac.Frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::PULSE: {
            const PulseParameters &pulse = source.GetPulse();
            if (pulse.High != pulse.Low) {
                const double frequency = pulse.Period > 0.0 ? 1.0 / pulse.Period : 0.0;
                if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::EXP:
        case Source::SourceType::SFFM:
        case Source::SourceType::AM:
        case Source::SourceType::PWL:
        case Source::SourceType::NOISE:
        case Source::SourceType::RANDOM:
        default:
            return std::format("{} uses a {} waveform, which harmonic balance does not support", part->GetName(),
                               GetSourceTypeName(source.GetSourceType()));
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency grid of a harmonic balance alone, without a circuit, as a file is read.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason the grid cannot be solved, or no value when it can.
 */
std::optional<std::string> CheckHarmonicBalanceGrid(const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(settings.FundamentalFrequency) || settings.FundamentalFrequency <= 0.0) {
        return "The fundamental frequency must be positive";
    }
    if (settings.Harmonics < 1 || settings.Harmonics > MAX_HARMONIC_BALANCE_HARMONICS) {
        return std::format("The number of harmonics must be between 1 and {}", MAX_HARMONIC_BALANCE_HARMONICS);
    }
    if (settings.Samples != 0 && (!IsPowerOfTwo(settings.Samples) || settings.Samples <= 2 * settings.Harmonics ||
                                  settings.Samples > MAX_HARMONIC_BALANCE_SAMPLES)) {
        return std::format("The sample count must be 0 (automatic) or a power of two greater than {} (up to {})",
                           2 * settings.Harmonics, MAX_HARMONIC_BALANCE_SAMPLES);
    }
    return std::nullopt;
}

/**
 * @brief   Finds which harmonic of a fundamental a frequency is.
 * @param[in] frequency    Frequency of a periodic source, in hertz.
 * @param[in] fundamental  Fundamental frequency, in hertz, above 0.
 * @return  The harmonic number, from 1, or none when the frequency is no integer multiple of the fundamental within
 *          HARMONIC_RATIO_TOLERANCE.
 */
std::optional<int> FindHarmonicNumber(const double frequency, const double fundamental) {
    const double ratio = frequency / fundamental;
    const double nearest = std::round(ratio);
    if (!std::isfinite(ratio) || nearest < 1.0 || std::abs(ratio - nearest) > HARMONIC_RATIO_TOLERANCE * nearest) {
        return std::nullopt;
    }
    return static_cast<int>(nearest);
}

} // namespace Core
