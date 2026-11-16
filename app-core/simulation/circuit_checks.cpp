/**
 * @file    circuit_checks.cpp
 * @brief   Checks of a circuit before any analysis runs on it.
 */

#include "simulation/circuit_checks.h"

#include "circuit/behavioral_expression.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/node_voltage_targets.h"
#include "circuit/subcircuit.h"
#include "simulation/check_solver_options.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

namespace {

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

/**
 * @brief   Names a node as the user sees it: V(OUT) when a label names its net, "node 3" otherwise.
 */
std::string DescribeNode(const Circuit &circuit, const int node) {
    for (const auto &[net, net_node] : circuit.GetNets()) {
        if (net_node == node) {
            return std::format("V({})", net);
        }
    }
    return std::format("node {}", node);
}

/// How the checks name a kind of marker in their messages.
struct NodeVoltageWords {
    std::string_view WithArticle;
    std::string_view Plural;
};

NodeVoltageWords DescribeKind(const NodeVoltageKind kind) {
    return kind == NodeVoltageKind::INITIAL
               ? NodeVoltageWords{.WithArticle = "an initial voltage", .Plural = "initial voltages"}
               : NodeVoltageWords{.WithArticle = "a nodeset", .Plural = "nodesets"};
}

/// The words of a message that starts with the marker, such as "An initial voltage".
std::string Capitalized(const std::string_view text) {
    std::string capitalized(text);
    capitalized.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(capitalized.front())));
    return capitalized;
}

/**
 * @brief   A marker needs a node of its own to act on: not ground, which is 0 V by definition, and a node some part of
 *          its circuit is connected to, since a loose one has no equations to start from.
 * @param[in] where  Where the circuit is, for the message: empty at the top, " in block NAME" inside a definition.
 */
std::optional<std::string> CheckNodeVoltagePlaces(const Circuit &circuit, const NodeVoltageKind kind,
                                                  const std::string_view where) {
    const NodeVoltageWords words = DescribeKind(kind);
    std::set<int> used_nodes;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        used_nodes.insert(entry.Nodes.begin(), entry.Nodes.end());
    }
    for (const CircuitNodeVoltage &marker : circuit.GetNodeVoltages(kind)) {
        if (marker.Node == 0) {
            return std::format("{}{} is on ground, which is always 0 V; place it on another node",
                               Capitalized(words.WithArticle), where);
        }
        if (!used_nodes.contains(marker.Node)) {
            return std::format("{} of {} V{} is on {}, which no part is connected to", Capitalized(words.WithArticle),
                               FormatValue(marker.Volts), where, DescribeNode(circuit, marker.Node));
        }
    }
    return std::nullopt;
}

/**
 * @brief   Names the node a target sets as the user sees it, with the instance it belongs to inside a block.
 */
std::string DescribeTarget(const NodeVoltageTarget &target) {
    const std::string node = DescribeNode(*target.Owner, target.Node);
    return target.Path.empty() ? node : std::format("{} of {}", node, FormatInstancePath(target.Path));
}

/**
 * @brief   One voltage per node once the blocks are expanded, which a marker on a pin of a block and another on the net
 *          it joins can break, and none on ground, which a pin tied to ground would reach.
 */
std::optional<std::string> CheckNodeVoltageTargets(const Circuit &circuit, const NodeVoltageKind kind) {
    const NodeVoltageWords words = DescribeKind(kind);
    std::map<std::pair<std::vector<std::string>, int>, const NodeVoltageTarget *> known_by_node;
    const std::vector<NodeVoltageTarget> targets = ResolveNodeVoltages(circuit, kind);
    for (const NodeVoltageTarget &target : targets) {
        const std::string where =
            target.MarkerPath.empty() ? "" : std::format(" in {}", FormatInstancePath(target.MarkerPath));
        if (target.Node == 0) {
            return std::format("{} of {} V{} reaches ground, which is always 0 V, through a pin that its instance "
                               "connects to ground",
                               Capitalized(words.WithArticle), FormatValue(target.Volts), where);
        }
        const auto [known, is_new] = known_by_node.try_emplace({target.Path, target.Node}, &target);
        if (!is_new && known->second->Volts != target.Volts) {
            const std::string known_where = known->second->MarkerPath.empty()
                                                ? ""
                                                : std::format(" in {}", FormatInstancePath(known->second->MarkerPath));
            return std::format("{} has two {}, {} V{} and {} V{}; keep one", DescribeTarget(target), words.Plural,
                               FormatValue(known->second->Volts), known_where, FormatValue(target.Volts), where);
        }
    }
    return std::nullopt;
}

/**
 * @brief   The first problem with the markers of a kind in the circuit and in the blocks it places.
 */
std::optional<std::string> CheckNodeVoltages(const Circuit &circuit, const NodeVoltageKind kind) {
    if (std::optional<std::string> problem = CheckNodeVoltagePlaces(circuit, kind, "")) {
        return problem;
    }
    for (const auto &definition : circuit.GetSubcircuits()) {
        if (std::optional<std::string> problem =
                CheckNodeVoltagePlaces(definition->Body, kind, std::format(" in block {}", definition->Name))) {
            return problem;
        }
    }
    return CheckNodeVoltageTargets(circuit, kind);
}

/**
 * @brief   Checks that every current-controlled source follows a current that some other part reports.
 */
std::optional<std::string> CheckControllingCurrents(const Circuit &circuit) {
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
 * @brief   Checks the behavioral sources of a circuit and of the bodies of its blocks, naming the block of a problem.
 */
std::optional<std::string> CheckAllBehavioralSources(const Circuit &circuit) {
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

} // namespace

/**
 * @brief   Checks that a circuit can be simulated at all.
 * @param[in] circuit  Circuit to check.
 * @return  The reason it cannot, in words for the user: it is empty, it has no ground (a circuit of only clocks, logic
 *          inputs, supply rails and logic parts, in blocks or not, needs none), a subcircuit instance or definition
 * cannot be written (an unknown definition, a pin on ground, a parameter it lacks), a part uses a library model the
 * circuit lacks or of another kind, an initial voltage is on ground, on a node nothing is connected to, inside a block
 * or twice with different voltages on one node, or a current-controlled source follows no valid current; no value when
 * it can.
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
    for (const NodeVoltageKind kind : {NodeVoltageKind::INITIAL, NodeVoltageKind::NODESET}) {
        if (std::optional<std::string> problem = CheckNodeVoltages(circuit, kind)) {
            return problem;
        }
    }
    if (std::optional<std::string> problem = CheckSolverOptions(circuit.GetSolverOptions())) {
        return problem;
    }
    if (std::optional<std::string> problem = CheckControllingCurrents(circuit)) {
        return problem;
    }
    return CheckAllBehavioralSources(circuit);
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

} // namespace Core
