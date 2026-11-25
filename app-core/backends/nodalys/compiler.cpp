/**
 * @file    compiler.cpp
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `Nodalys::Circuit`.
 */

#include "backends/nodalys/compiler.h"

#include "backends/nodalys/compiler_context.h"
#include "backends/nodalys/compiler_logic.h"
#include "backends/nodalys/compiler_parts.h"
#include "backends/nodalys/spice_block_check.h"
#include "circuit/behavioral_expression.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/spice_library.h"
#include "circuit/subcircuit.h"
#include <algorithm>
#include <format>
#include <set>

namespace Core {

namespace {

/**
 * @brief   Plans a probe for every terminal whose current a controlled or a behavioral source reads, one for each
 *          terminal however many sources read it.
 */
std::expected<std::vector<ProbeSpec>, std::string> PlanProbes(const Circuit &circuit) {
    std::vector<ProbeSpec> probes;
    const auto add_probe = [&probes](const ProbeSpec &target) {
        const bool is_planned = std::ranges::any_of(probes, [&target](const ProbeSpec &probe) {
            return probe.Entry == target.Entry && probe.Terminal == target.Terminal;
        });
        if (!is_planned) {
            probes.push_back(target);
        }
    };
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        std::vector<std::string> currents;
        if (IsCurrentControlled(entry.Part->GetType())) {
            currents.push_back(static_cast<const ControlledSource &>(*entry.Part).GetControllingCurrent());
        } else if (IsBehavioralSource(entry.Part->GetType())) {
            currents = ListBehavioralCurrents(static_cast<const BehavioralSource &>(*entry.Part).GetExpression());
        }
        for (const std::string &current : currents) {
            const auto target = FindControllingCurrent(circuit, current);
            if (!target) {
                return std::unexpected(target.error());
            }
            add_probe(*target);
        }
    }
    return probes;
}

std::set<int> UsedNodes(const Circuit &circuit) {
    std::set<int> used;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const int node : entry.Nodes) {
            if (node > 0) {
                used.insert(node);
            }
        }
    }
    return used;
}

std::expected<double, std::string> EffectiveValue(const Component &part, const Parameters &parameters) {
    const std::optional<std::string> &expression = part.GetValueExpression();
    if (!expression) {
        return part.GetValue();
    }
    std::string text = *expression;
    for (std::size_t open = text.find('{'); open != std::string::npos; open = text.find('{')) {
        const std::size_t close = text.find('}', open);
        if (close == std::string::npos) {
            return std::unexpected(
                std::format("{} has an unbalanced parameter reference {}", part.GetName(), *expression));
        }
        const std::string name = text.substr(open + 1, close - open - 1);
        const auto value = parameters.find(name);
        if (value == parameters.end()) {
            return std::unexpected(
                std::format("{} refers to the parameter {{{}}} of its definition, which the instance does not pass",
                            part.GetName(),
                            name));
        }
        text.replace(open, close - open + 1, std::format("{:.17g}", value->second));
    }
    if (const std::optional<double> parsed = ParseSpiceNumber(text)) {
        return *parsed;
    }
    return std::unexpected(
        std::format("{} uses the value expression {}, which Nodalys cannot evaluate", part.GetName(), *expression));
}

Nodalys::SubcircuitText MakeSpiceText(const std::string &name, const SubcircuitText &spice) {
    return {.Name = name, .Ports = spice.Ports, .Body = spice.Text};
}

/**
 * @brief   The value of each parameter an instance of a block is used with: the defaults of the definition, then what
 *          the instance sets.
 */
Parameters InstanceParameters(const SubcircuitInstance &instance, const SubcircuitDefinition &definition) {
    Parameters values;
    for (const SubcircuitParameter &parameter : definition.Parameters) {
        values[parameter.Name] = parameter.DefaultValue;
    }
    for (const auto &[name, value] : instance.GetParameters()) {
        values[name] = value;
    }
    return values;
}

/**
 * @brief   Tells whether a part is built from its value, which may be an expression of the parameters of its block.
 */
bool HasEffectiveValue(const ComponentType type) {
    switch (type) {
    case ComponentType::RESISTOR:
    case ComponentType::CAPACITOR:
    case ComponentType::INDUCTOR:
    case ComponentType::VCC:
    case ComponentType::POTENTIOMETER:
    case ComponentType::VCVS:
    case ComponentType::VCCS:
    case ComponentType::CCCS:
    case ComponentType::CCVS:
        return true;
    default:
        return false;
    }
}

/**
 * @brief   Works out the name, the nodes and the value of one entry of a scope.
 */
std::expected<EntryContext, std::string> ResolveEntry(const CompileScope &scope, const int entry_index) {
    const CircuitEntry &entry = scope.Owner.GetEntries()[static_cast<std::size_t>(entry_index)];
    const Component &part = *entry.Part;
    EntryContext context{
        .Part = part,
        .Type = part.GetType(),
        .Name = scope.Prefix.empty() ? part.GetName() : std::format("{}.{}", scope.Prefix, part.GetName()),
        .Nodes = {},
        .Value = 0.0,
    };

    context.Nodes.reserve(entry.Nodes.size());
    for (const int local_node : entry.Nodes) {
        context.Nodes.push_back(scope.NodeMap[static_cast<std::size_t>(local_node)]);
    }
    if (const auto probes = scope.Probes.find(entry_index); probes != scope.Probes.end()) {
        for (const auto &[terminal, probe_node] : probes->second) {
            context.Nodes[static_cast<std::size_t>(terminal)] = probe_node;
        }
    }

    if (HasEffectiveValue(context.Type)) {
        const auto value = EffectiveValue(part, scope.Values);
        if (!value) {
            return std::unexpected(value.error());
        }
        context.Value = *value;
    }
    return context;
}

class CircuitCompiler {
public:
    explicit CircuitCompiler(const Circuit &circuit) : m_Circuit(circuit) {}

    std::expected<Nodalys::Circuit, std::string> Compile() {
        const int circuit_nodes = m_Circuit.GetNodeCount();
        m_Builder = std::make_unique<Nodalys::CircuitBuilder>(circuit_nodes);

        std::vector<int> top_node_map(static_cast<std::size_t>(circuit_nodes), 0);
        for (int node = 1; node < circuit_nodes; ++node) {
            top_node_map[static_cast<std::size_t>(node)] = node;
        }

        if (const auto result = AddCircuit(m_Circuit, top_node_map, {}); !result) {
            return std::unexpected(result.error());
        }

        for (const auto &port : m_Circuit.GetPorts()) {
            m_Builder->AddPort(port.Number, port.Node, port.ReferenceOhms);
        }
        AddNodeVoltages(m_Circuit, top_node_map);

        return m_Builder->Build();
    }

    std::vector<std::string> TakeWarnings() { return std::move(m_Warnings); }

private:
    const Circuit &m_Circuit;
    std::unique_ptr<Nodalys::CircuitBuilder> m_Builder;
    std::vector<std::string> m_Warnings;

    /// Hands the engine the initial voltages and the nodesets of a circuit, at the nodes its node map gives.
    void AddNodeVoltages(const Circuit &circuit, const std::vector<int> &node_map) {
        for (const CircuitNodeVoltage &initial_voltage : circuit.GetInitialVoltages()) {
            m_Builder->SetInitialVoltage(node_map[static_cast<std::size_t>(initial_voltage.Node)],
                                         initial_voltage.Volts);
        }
        for (const CircuitNodeVoltage &nodeset : circuit.GetNodesets()) {
            m_Builder->SetNodeset(node_map[static_cast<std::size_t>(nodeset.Node)], nodeset.Volts);
        }
    }

    std::expected<void, std::string> AddCircuit(const Circuit &circuit,
                                                const std::vector<int> &node_map,
                                                const Parameters &parameters,
                                                const std::string &prefix = "") {
        const auto probes = PlanProbes(circuit);
        if (!probes) {
            return std::unexpected(probes.error());
        }

        CompileScope scope{.Owner = circuit, .NodeMap = node_map, .Values = parameters, .Prefix = prefix, .Probes = {}};
        for (const ProbeSpec &probe : *probes) {
            const CircuitEntry &target = circuit.GetEntries()[static_cast<std::size_t>(probe.Entry)];
            const int original_node =
                node_map[static_cast<std::size_t>(target.Nodes[static_cast<std::size_t>(probe.Terminal)])];
            const int probe_node = m_Builder->AddNode();
            m_Builder->AddCurrentProbe(ProbeName(prefix, probe.Entry, probe.Terminal), original_node, probe_node);
            scope.Probes[probe.Entry][probe.Terminal] = probe_node;
        }

        for (std::size_t index = 0; index < circuit.GetEntries().size(); ++index) {
            if (const auto result = AddEntry(scope, static_cast<int>(index)); !result) {
                return std::unexpected(result.error());
            }
        }
        return {};
    }

    std::expected<void, std::string> AddEntry(const CompileScope &scope, const int entry_index) {
        const auto resolved = ResolveEntry(scope, entry_index);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        const EntryContext &entry = *resolved;
        switch (GetFamily(entry.Type)) {
        case ComponentFamily::MARKER:
            return {};
        case ComponentFamily::PASSIVE:
            return AddPassivePart(*m_Builder, entry);
        case ComponentFamily::SOURCE:
            return AddSourcePart(*m_Builder, entry);
        case ComponentFamily::CONTROLLED_SOURCE:
            return AddControlledSource(*m_Builder, entry, scope);
        case ComponentFamily::BEHAVIORAL_SOURCE:
            return AddBehavioralSource(*m_Builder, entry, scope);
        case ComponentFamily::SEMICONDUCTOR:
            return AddSemiconductor(*m_Builder, m_Circuit, entry);
        case ComponentFamily::SWITCHING:
            return AddSwitchingPart(*m_Builder, entry);
        case ComponentFamily::TRANSFORMER:
            return AddTransformer(*m_Builder, entry), std::expected<void, std::string>{};
        case ComponentFamily::TRANSMISSION_LINE:
            return AddTransmissionLine(*m_Builder, entry), std::expected<void, std::string>{};
        case ComponentFamily::AMPLIFIER:
            return AddOpAmp(*m_Builder, entry), std::expected<void, std::string>{};
        case ComponentFamily::BLOCK:
            return AddSubcircuitInstance(entry, scope);
        case ComponentFamily::LOGIC_GATE:
            return AddGatePart(*m_Builder, entry), std::expected<void, std::string>{};
        case ComponentFamily::FLIP_FLOP:
            return AddFlipFlopPart(*m_Builder, entry), std::expected<void, std::string>{};
        case ComponentFamily::LOGIC_DISPLAY:
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    /**
     * @brief   Hands a block kept as SPICE text to the engine, which reads it as an instance of its own.
     */
    std::expected<void, std::string> AddSpiceBlock(const SubcircuitDefinition &definition,
                                                   const SubcircuitText &spice,
                                                   const SubcircuitInstance &instance,
                                                   const std::vector<int> &nodes,
                                                   const std::string &instance_prefix) {
        const auto added = Nodalys::AddSubcircuitText(
            *m_Builder,
            MakeSpiceText(definition.Name, spice),
            {.Prefix = instance_prefix, .PortNodes = nodes, .Parameters = InstanceParameters(instance, definition)});
        if (!added) {
            return std::unexpected(added.error());
        }
        m_Warnings.insert(m_Warnings.end(), added->begin(), added->end());
        return {};
    }

    /**
     * @brief   Compiles the body of a block into the circuit, with the pins on the nodes of the instance and a fresh
     *          node for every other node the body uses; a block kept as SPICE text goes to the engine as it is.
     */
    std::expected<void, std::string> AddSubcircuitInstance(const EntryContext &entry, const CompileScope &scope) {
        const auto &instance = static_cast<const SubcircuitInstance &>(entry.Part);
        const std::shared_ptr<const SubcircuitDefinition> definition =
            m_Circuit.FindSubcircuit(instance.GetDefinitionName());
        if (!definition) {
            return std::unexpected(std::format("The subcircuit {} is not defined", instance.GetDefinitionName()));
        }
        const std::string sub_prefix =
            scope.Prefix.empty() ? instance.GetName() : std::format("{}.{}", scope.Prefix, instance.GetName());
        if (definition->Spice) {
            return AddSpiceBlock(*definition, *definition->Spice, instance, entry.Nodes, sub_prefix);
        }
        const std::set<int> used = UsedNodes(definition->Body);
        std::vector<int> body_map(static_cast<std::size_t>(definition->Body.GetNodeCount()), 0);
        for (std::size_t pin = 0; pin < definition->Pins.size() && pin < entry.Nodes.size(); ++pin) {
            body_map[static_cast<std::size_t>(definition->Pins[pin].Node)] = entry.Nodes[pin];
        }
        for (const int body_node : used) {
            if (body_map[static_cast<std::size_t>(body_node)] == 0
                && std::ranges::none_of(definition->Pins,
                                        [body_node](const SubcircuitPin &p) { return p.Node == body_node; })) {
                body_map[static_cast<std::size_t>(body_node)] = m_Builder->AddNode();
            }
        }
        // A marker in the body acts on this instance's own node, or on the outer node when it sits on a pin
        AddNodeVoltages(definition->Body, body_map);
        return AddCircuit(definition->Body, body_map, InstanceParameters(instance, *definition), sub_prefix);
    }
};

} // namespace

/**
 * @brief   Compiles a circuit for Nodalys.
 * @param[in]  circuit    Circuit to compile.
 * @param[out] warnings   Receives what the engine says about the blocks kept as SPICE text it read, or null to
 *                        ignore it.
 * @return  The compiled circuit, or why the circuit cannot be simulated by Nodalys.
 */
std::expected<Nodalys::Circuit, std::string> CompileCircuit(const Circuit &circuit,
                                                            std::vector<std::string> *warnings) {
    CircuitCompiler compiler(circuit);
    auto compiled = compiler.Compile();
    if (warnings != nullptr) {
        *warnings = compiler.TakeWarnings();
    }
    return compiled;
}

/**
 * @brief   Tells why Nodalys cannot simulate a block of SPICE text, before a run asks it to.
 * @param[in] circuit   Circuit that holds the definition of the block.
 * @param[in] instance  The use of the block.
 * @return  The reason, written for the user, or none when the block can be simulated or is drawn.
 */
std::optional<std::string> FindSpiceBlockProblem(const Circuit &circuit, const SubcircuitInstance &instance) {
    const std::shared_ptr<const SubcircuitDefinition> definition = circuit.FindSubcircuit(instance.GetDefinitionName());
    if (!definition || !definition->Spice) {
        return std::nullopt;
    }
    return GetNodalysSpiceBlockCheck().Find(MakeSpiceText(definition->Name, *definition->Spice),
                                            InstanceParameters(instance, *definition));
}

} // namespace Core
