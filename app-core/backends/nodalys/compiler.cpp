/**
 * @file    compiler.cpp
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `Nodalys::Circuit`.
 */

#include "backends/nodalys/compiler.h"

#include "backends/nodalys/model_parser.h"
#include "backends/nodalys/parameter_conversion.h"
#include "backends/nodalys/spice_block_check.h"
#include "circuit/behavioral_expression.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/switch.h"
#include "circuit/components/transformer.h"
#include "circuit/components/voltage_controlled_switch.h"
#include "circuit/spice_library.h"
#include "circuit/subcircuit.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace Core {

namespace {

using Parameters = std::map<std::string, double>;

struct ProbeSpec {
    int Entry;
    int Terminal;
};

std::expected<ProbeSpec, std::string> FindControllingCurrent(const Circuit &circuit, const std::string &name) {
    for (std::size_t entry = 0; entry < circuit.GetEntries().size(); ++entry) {
        const Component &part = *circuit.GetEntries()[entry].Part;
        const std::vector<std::string> names = GetCurrentNames(part);
        const std::vector<std::size_t> terminals = GetCurrentTerminals(part);
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (names[index] == name) {
                return ProbeSpec{static_cast<int>(entry), static_cast<int>(terminals[index])};
            }
        }
    }
    return std::unexpected(std::format("The circuit has no part reporting the current {}", name));
}

/**
 * @brief   Names the 0 V source that carries the current of one terminal of a part, inside the block named by prefix.
 */
std::string ProbeName(const std::string &prefix, const int entry, const int terminal) {
    return prefix.empty() ? std::format("__probe_{}_{}", entry, terminal)
                          : std::format("__probe_{}_{}_{}", prefix, entry, terminal);
}

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

Nodalys::SmoothLogicParameters MakeLogicParams(const LogicPart &part) {
    const auto &params = part.GetParameters();
    return {
        .VLow = 0.0,
        .VHigh = params.LogicLevel,
        .VThreshold = 0.5 * params.LogicLevel,
        .TransitionWidth = 0.1 * params.LogicLevel,
        .PropagationDelay = params.PropagationDelay,
        .OutputResistance = 50.0,
        .InputResistance = 1e9,
    };
}

Nodalys::BoyleOpAmpParameters MakeBoyleParams(const OpAmpParameters &params) {
    return {
        .IsPnpInput = params.InputStage == OpAmpInputStage::PNP,
        .OpenLoopGainDecibels = params.OpenLoopGainDecibels,
        .GainBandwidth = params.GainBandwidth,
        .SlewRate = params.SlewRate,
        .PhaseMarginDegrees = params.PhaseMarginDegrees,
        .InputBiasCurrent = params.InputBiasCurrent,
        .CommonModeRejectionDecibels = params.CommonModeRejectionDecibels,
        .OutputResistance = params.OutputResistance,
        .ShortCircuitCurrent = params.ShortCircuitCurrent,
        .PositiveHeadroom = params.PositiveHeadroom,
        .NegativeHeadroom = params.NegativeHeadroom,
        .SupplyCurrent = params.SupplyCurrent,
        .InputOffsetVoltage = params.InputOffsetVoltage,
    };
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

using ProbeNodes = std::map<int, std::map<int, int>>;

/**
 * @struct  CompileScope
 * @brief   One circuit level being compiled: the top circuit or the body of one subcircuit instance.
 */
struct CompileScope {
    /// The circuit whose entries are compiled, where the nets and the controlling currents are looked up.
    const Circuit &Owner;
    /// The node of the compiled circuit that each node of the owner stands for.
    const std::vector<int> &NodeMap;
    /// The value of each parameter the instance of this level passes; empty at the top.
    const Parameters &Values;
    /// Dotted name of the instance, empty at the top, which keeps the names of the devices apart between instances.
    const std::string &Prefix;
    /// entry_index -> terminal_index -> probe_node, the nodes that replace a terminal whose current is read.
    ProbeNodes Probes;
};

/**
 * @struct  EntryContext
 * @brief   What every device of one entry needs, worked out once: the name, the nodes in the compiled circuit and the
 *          value.
 */
struct EntryContext {
    const Component &Part;
    ComponentType Type;
    std::string Name;
    /// The compiled nodes of the terminals, with a probe node in place of each probed terminal.
    std::vector<int> Nodes;
    /// The value with its parameter references resolved; zero for a part that has none.
    double Value = 0.0;
};

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
 * @brief   Maps the type of a gate part to the gate of the engine.
 */
Nodalys::LogicGateType GetGateType(const ComponentType type) {
    switch (type) {
    case ComponentType::NAND:
        return Nodalys::LogicGateType::NAND;
    case ComponentType::OR:
        return Nodalys::LogicGateType::OR;
    case ComponentType::NOR:
        return Nodalys::LogicGateType::NOR;
    case ComponentType::XOR:
        return Nodalys::LogicGateType::XOR;
    case ComponentType::XNOR:
        return Nodalys::LogicGateType::XNOR;
    case ComponentType::AND:
    default:
        return Nodalys::LogicGateType::AND;
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

    // Circuit levels
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

    // Entries
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
            return AddPassivePart(entry);
        case ComponentFamily::SOURCE:
            return AddSourcePart(entry);
        case ComponentFamily::CONTROLLED_SOURCE:
            return AddControlledSource(entry, scope);
        case ComponentFamily::BEHAVIORAL_SOURCE:
            return AddBehavioralSource(entry, scope);
        case ComponentFamily::SEMICONDUCTOR:
            return AddSemiconductor(entry);
        case ComponentFamily::SWITCHING:
            return AddSwitchingPart(entry);
        case ComponentFamily::TRANSFORMER:
            AddTransformer(entry);
            return {};
        case ComponentFamily::AMPLIFIER:
            AddOpAmp(entry);
            return {};
        case ComponentFamily::BLOCK:
            return AddSubcircuitInstance(entry, scope);
        case ComponentFamily::LOGIC_GATE:
            AddGatePart(entry);
            return {};
        case ComponentFamily::FLIP_FLOP:
            AddFlipFlopPart(entry);
            return {};
        case ComponentFamily::LOGIC_DISPLAY:
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    static std::string NoDeviceMessage(const EntryContext &entry) {
        return std::format("Nodalys has no device for {} (a {} part) yet",
                           entry.Part.GetName(),
                           GetTypeName(entry.Type));
    }

    // Families
    std::expected<void, std::string> AddPassivePart(const EntryContext &entry) {
        const Component &part = entry.Part;
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::RESISTOR:
            m_Builder->AddResistor(entry.Name, nodes[0], nodes[1], entry.Value);
            return {};
        case ComponentType::CAPACITOR:
            m_Builder->AddCapacitor(entry.Name,
                                    nodes[0],
                                    nodes[1],
                                    entry.Value,
                                    part.GetInitialCondition().value_or(0.0));
            return {};
        case ComponentType::INDUCTOR:
            m_Builder->AddInductor(entry.Name,
                                   nodes[0],
                                   nodes[1],
                                   entry.Value,
                                   part.GetInitialCondition().value_or(0.0));
            return {};
        case ComponentType::SWITCH:
        case ComponentType::PUSH_BUTTON:
            m_Builder->AddResistor(entry.Name, nodes[0], nodes[1], static_cast<const Switch &>(part).GetResistance());
            return {};
        case ComponentType::POTENTIOMETER:
            m_Builder->AddPotentiometer(entry.Name,
                                        nodes[0],
                                        nodes[2],
                                        nodes[1],
                                        entry.Value,
                                        static_cast<const Potentiometer &>(part).GetWiper());
            return {};
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    std::expected<void, std::string> AddSourcePart(const EntryContext &entry) {
        const Component &part = entry.Part;
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::VCC:
            m_Builder->AddVdc(entry.Name, nodes[0], 0, entry.Value);
            return {};
        case ComponentType::VOLTAGE_SOURCE:
        case ComponentType::CURRENT_SOURCE:
            AddSource(entry);
            return {};
        case ComponentType::CLOCK:
            m_Builder->AddClock(entry.Name, nodes[0], 0, ToNodalys(static_cast<const Clock &>(part).GetParameters()));
            return {};
        case ComponentType::LOGIC_INPUT: {
            const auto &input = static_cast<const LogicInput &>(part);
            m_Builder->AddLogicInput(entry.Name, nodes[0], 0, input.IsHigh(), input.GetParameters().LogicLevel, 0.0);
            return {};
        }
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    std::expected<void, std::string> AddSemiconductor(const EntryContext &entry) {
        const Component &part = entry.Part;
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::DIODE:
        case ComponentType::ZENER_DIODE:
        case ComponentType::LED: {
            const auto resolved_diode = ResolveParameters(m_Circuit, static_cast<const Diode &>(part));
            if (!resolved_diode) {
                return std::unexpected(resolved_diode.error());
            }
            m_Builder->AddDiode(entry.Name, nodes[0], nodes[1], ToNodalys(*resolved_diode));
            return {};
        }
        case ComponentType::NPN:
        case ComponentType::PNP: {
            const auto resolved_bjt = ResolveParameters(m_Circuit, static_cast<const BJT &>(part));
            if (!resolved_bjt) {
                return std::unexpected(resolved_bjt.error());
            }
            m_Builder->AddBjt(entry.Name,
                              nodes[0],
                              nodes[1],
                              nodes[2],
                              entry.Type != ComponentType::PNP,
                              ToNodalys(*resolved_bjt));
            return {};
        }
        case ComponentType::NMOS:
        case ComponentType::PMOS: {
            const auto resolved_mosfet = ResolveParameters(m_Circuit, static_cast<const MOSFET &>(part));
            if (!resolved_mosfet) {
                return std::unexpected(resolved_mosfet.error());
            }
            const int body = nodes.size() > 3 ? nodes[3] : nodes[2];
            m_Builder->AddMosfet(entry.Name,
                                 nodes[0],
                                 nodes[1],
                                 nodes[2],
                                 body,
                                 entry.Type != ComponentType::PMOS,
                                 ToNodalys(*resolved_mosfet));
            return {};
        }
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    std::expected<void, std::string> AddSwitchingPart(const EntryContext &entry) {
        const Component &part = entry.Part;
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::V_SWITCH:
            m_Builder->AddVoltageControlledSwitch(
                entry.Name,
                nodes[0],
                nodes[1],
                nodes[2],
                nodes[3],
                ToNodalys(static_cast<const VoltageControlledSwitch &>(part).GetParameters()));
            return {};
        case ComponentType::FUSE:
            m_Builder->AddFuse(entry.Name,
                               nodes[0],
                               nodes[1],
                               ToNodalys(static_cast<const Fuse &>(part).GetParameters()));
            return {};
        case ComponentType::RELAY: {
            const auto &relay = static_cast<const Relay &>(part);
            const RelayParameters &figures = relay.GetParameters();
            m_Builder->AddRelay(entry.Name,
                                nodes[2],
                                nodes[3],
                                nodes[0],
                                nodes[1],
                                figures.CoilResistance,
                                figures.CoilInductance,
                                ToNodalys(relay.GetContactParameters()));
            return {};
        }
        default:
            return std::unexpected(NoDeviceMessage(entry));
        }
    }

    void AddTransformer(const EntryContext &entry) {
        const auto &transformer = static_cast<const Transformer &>(entry.Part);
        const TransformerParameters &figures = transformer.GetParameters();
        m_Builder->AddTransformer(entry.Name,
                                  entry.Nodes[0],
                                  entry.Nodes[1],
                                  entry.Nodes[2],
                                  entry.Nodes[3],
                                  transformer.GetValue(),
                                  transformer.GetSecondaryInductance(),
                                  figures.Coupling,
                                  figures.WindingResistance);
    }

    /**
     * @brief   Adds a logic gate: the inverter and the buffer have one input; every other gate is one device that
     *          AddLogicGate() builds.
     */
    void AddGatePart(const EntryContext &entry) {
        const auto &logic = static_cast<const LogicPart &>(entry.Part);
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::NOT:
            m_Builder->AddInverter(entry.Name, nodes[1], nodes[0], MakeLogicParams(logic));
            break;
        case ComponentType::BUFFER:
            m_Builder->AddLogicBuffer(entry.Name, nodes[1], nodes[0], MakeLogicParams(logic));
            break;
        default:
            AddLogicGate(entry);
            break;
        }
    }

    void AddFlipFlopPart(const EntryContext &entry) {
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::SR_FLIP_FLOP:
            ExpandSrFlipFlop(entry);
            break;
        case ComponentType::T_FLIP_FLOP:
            ExpandTFlipFlop(entry);
            break;
        case ComponentType::JK_FLIP_FLOP:
            ExpandJkFlipFlop(entry);
            break;
        default:
            m_Builder->AddDFlipFlop(entry.Name,
                                    nodes[4],
                                    nodes[5],
                                    nodes[0],
                                    nodes[1],
                                    MakeLogicParams(static_cast<const LogicPart &>(entry.Part)),
                                    nodes[2],
                                    nodes[3]);
            break;
        }
    }

    // Blocks
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

    // Sources
    /**
     * @brief   Adds a voltage or a current source of any waveform; the two kinds differ only in the builder method.
     */
    void AddSource(const EntryContext &entry) {
        const auto &source = static_cast<const Source &>(entry.Part);
        const bool is_voltage = entry.Type == ComponentType::VOLTAGE_SOURCE;
        const std::string &name = entry.Name;
        const int positive = entry.Nodes[0];
        const int negative = entry.Nodes[1];
        const double ac_magnitude = source.GetACMagnitude();
        switch (source.GetSourceType()) {
        case Source::SourceType::DC:
            if (is_voltage) {
                m_Builder->AddVdc(name, positive, negative, source.GetValue(), ac_magnitude);
            } else {
                m_Builder->AddIdc(name, positive, negative, source.GetValue(), ac_magnitude);
            }
            break;
        case Source::SourceType::AC:
            if (is_voltage) {
                m_Builder->AddVsine(name, positive, negative, ToNodalys(source.GetAC()), ac_magnitude);
            } else {
                m_Builder->AddIsine(name, positive, negative, ToNodalys(source.GetAC()), ac_magnitude);
            }
            break;
        case Source::SourceType::PULSE:
            if (is_voltage) {
                m_Builder->AddVpulse(name, positive, negative, ToNodalys(source.GetPulse()), ac_magnitude);
            } else {
                m_Builder->AddIpulse(name, positive, negative, ToNodalys(source.GetPulse()), ac_magnitude);
            }
            break;
        case Source::SourceType::EXP:
            if (is_voltage) {
                m_Builder->AddVexp(name, positive, negative, ToNodalys(source.GetEXP()), ac_magnitude);
            } else {
                m_Builder->AddIexp(name, positive, negative, ToNodalys(source.GetEXP()), ac_magnitude);
            }
            break;
        case Source::SourceType::PWL:
            if (is_voltage) {
                m_Builder->AddVpwl(name, positive, negative, ToNodalys(source.GetPWL()), ac_magnitude);
            } else {
                m_Builder->AddIpwl(name, positive, negative, ToNodalys(source.GetPWL()), ac_magnitude);
            }
            break;
        case Source::SourceType::SFFM:
            if (is_voltage) {
                m_Builder->AddVsffm(name, positive, negative, ToNodalys(source.GetSFFM()), ac_magnitude);
            } else {
                m_Builder->AddIsffm(name, positive, negative, ToNodalys(source.GetSFFM()), ac_magnitude);
            }
            break;
        case Source::SourceType::AM:
            if (is_voltage) {
                m_Builder->AddVam(name, positive, negative, ToNodalys(source.GetAM()), ac_magnitude);
            } else {
                m_Builder->AddIam(name, positive, negative, ToNodalys(source.GetAM()), ac_magnitude);
            }
            break;
        case Source::SourceType::NOISE:
            if (is_voltage) {
                m_Builder->AddVnoise(name, positive, negative, ToNodalys(source.GetNoise()), ac_magnitude);
            } else {
                m_Builder->AddInoise(name, positive, negative, ToNodalys(source.GetNoise()), ac_magnitude);
            }
            break;
        case Source::SourceType::RANDOM:
        default:
            if (is_voltage) {
                m_Builder->AddVrandom(name, positive, negative, ToNodalys(source.GetRandom()), ac_magnitude);
            } else {
                m_Builder->AddIrandom(name, positive, negative, ToNodalys(source.GetRandom()), ac_magnitude);
            }
            break;
        }
    }

    /**
     * @brief   Adds a source controlled by a voltage or by a current; the current ones read the probe planned for the
     *          controlling current.
     */
    std::expected<void, std::string> AddControlledSource(const EntryContext &entry, const CompileScope &scope) {
        const std::vector<int> &nodes = entry.Nodes;
        switch (entry.Type) {
        case ComponentType::VCVS:
            m_Builder->AddVcvs(entry.Name, nodes[0], nodes[1], nodes[2], nodes[3], entry.Value);
            break;
        case ComponentType::VCCS:
            m_Builder->AddVccs(entry.Name, nodes[0], nodes[1], nodes[2], nodes[3], entry.Value);
            break;
        case ComponentType::CCCS:
        case ComponentType::CCVS: {
            const auto &source = static_cast<const ControlledSource &>(entry.Part);
            const auto target = FindControllingCurrent(scope.Owner, source.GetControllingCurrent());
            if (!target) {
                return std::unexpected(target.error());
            }
            const std::string probe_name = ProbeName(scope.Prefix, target->Entry, target->Terminal);
            if (entry.Type == ComponentType::CCCS) {
                m_Builder->AddCccs(entry.Name, nodes[0], nodes[1], probe_name, entry.Value);
            } else {
                m_Builder->AddCcvs(entry.Name, nodes[0], nodes[1], probe_name, entry.Value);
            }
            break;
        }
        default:
            break;
        }
        return {};
    }

    /**
     * @brief   Adds a behavioral source, with the nets of its expression turned into nodes and the currents it reads
     *          into the probes planned for them, so the engine gets an expression of plain node numbers and source
     *          names.
     */
    std::expected<void, std::string> AddBehavioralSource(const EntryContext &entry, const CompileScope &scope) {
        const auto expression = TranslateBehavioralExpression(
            static_cast<const BehavioralSource &>(entry.Part).GetExpression(),
            [&scope](const std::string_view net) -> std::optional<int> {
                const std::optional<int> node = scope.Owner.FindNodeOfNet(net);
                return node ? std::optional<int>(scope.NodeMap[static_cast<std::size_t>(*node)]) : std::nullopt;
            },
            [&scope](const std::string_view current) -> std::optional<std::string> {
                const auto target = FindControllingCurrent(scope.Owner, std::string(current));
                return target ? std::optional<std::string>(ProbeName(scope.Prefix, target->Entry, target->Terminal))
                              : std::nullopt;
            });
        if (!expression) {
            return std::unexpected(std::format("{}: {}", entry.Name, expression.error()));
        }
        if (entry.Type == ComponentType::BEHAVIORAL_VOLTAGE) {
            m_Builder->AddBehavioralVoltage(entry.Name, entry.Nodes[0], entry.Nodes[1], *expression);
        } else {
            m_Builder->AddBehavioralCurrent(entry.Name, entry.Nodes[0], entry.Nodes[1], *expression);
        }
        return {};
    }

    // Logic
    void AddLogicGate(const EntryContext &entry) {
        const auto &logic = static_cast<const LogicPart &>(entry.Part);
        const std::size_t input_count = static_cast<std::size_t>(logic.GetInputCount());
        const std::vector<Nodalys::NodeId> inputs(entry.Nodes.begin(),
                                                  entry.Nodes.begin() + static_cast<std::ptrdiff_t>(input_count));
        m_Builder->AddLogicGate(entry.Name,
                                GetGateType(entry.Type),
                                entry.Nodes[input_count],
                                inputs,
                                MakeLogicParams(logic));
    }

    /**
     * @brief   Builds a set-reset flip-flop from a D flip-flop, whose D input is S OR (NOT R AND Q).
     */
    void ExpandSrFlipFlop(const EntryContext &entry) {
        const std::vector<int> &nodes = entry.Nodes;
        const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
        const int node_not_r = m_Builder->AddNode();
        const int node_and = m_Builder->AddNode();
        const int node_d = m_Builder->AddNode();
        m_Builder->AddInverter(entry.Name + ".NOT_R", node_not_r, nodes[1], params);
        m_Builder->AddLogicGate(entry.Name + ".AND",
                                Nodalys::LogicGateType::AND,
                                node_and,
                                {node_not_r, nodes[5]},
                                params);
        m_Builder->AddLogicGate(entry.Name + ".OR", Nodalys::LogicGateType::OR, node_d, {nodes[0], node_and}, params);
        m_Builder->AddDFlipFlop(entry.Name, nodes[5], nodes[6], node_d, nodes[2], params, nodes[3], nodes[4]);
    }

    /**
     * @brief   Builds a toggle flip-flop from a D flip-flop, whose D input is T XOR Q.
     */
    void ExpandTFlipFlop(const EntryContext &entry) {
        const std::vector<int> &nodes = entry.Nodes;
        const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
        const int internal_d = m_Builder->AddNode();
        m_Builder->AddLogicGate(entry.Name + ".XOR",
                                Nodalys::LogicGateType::XOR,
                                internal_d,
                                {nodes[0], nodes[4]},
                                params);
        m_Builder->AddDFlipFlop(entry.Name, nodes[4], nodes[5], internal_d, nodes[1], params, nodes[2], nodes[3]);
    }

    /**
     * @brief   Builds a JK flip-flop from a D flip-flop, whose D input is (J AND NOT Q) OR (NOT K AND Q).
     */
    void ExpandJkFlipFlop(const EntryContext &entry) {
        const std::vector<int> &nodes = entry.Nodes;
        const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
        const int node_not_k = m_Builder->AddNode();
        const int node_and1 = m_Builder->AddNode();
        const int node_and2 = m_Builder->AddNode();
        const int node_d = m_Builder->AddNode();
        m_Builder->AddInverter(entry.Name + ".NOT_K", node_not_k, nodes[1], params);
        m_Builder->AddLogicGate(entry.Name + ".AND1",
                                Nodalys::LogicGateType::AND,
                                node_and1,
                                {nodes[0], nodes[6]},
                                params);
        m_Builder->AddLogicGate(entry.Name + ".AND2",
                                Nodalys::LogicGateType::AND,
                                node_and2,
                                {node_not_k, nodes[5]},
                                params);
        m_Builder->AddLogicGate(entry.Name + ".OR", Nodalys::LogicGateType::OR, node_d, {node_and1, node_and2}, params);
        m_Builder->AddDFlipFlop(entry.Name, nodes[5], nodes[6], node_d, nodes[2], params, nodes[3], nodes[4]);
    }

    // Amplifiers
    void AddOpAmp(const EntryContext &entry) {
        const std::vector<int> &nodes = entry.Nodes;
        const auto &op_amp = static_cast<const OpAmp &>(entry.Part);
        if (op_amp.IsIdeal()) {
            m_Builder->AddIdealOpAmp(entry.Name,
                                     nodes[0],
                                     nodes[1],
                                     nodes[2],
                                     nodes[3],
                                     nodes[4],
                                     op_amp.GetIdealBandwidth());
        } else {
            m_Builder->AddBoyleOpAmp(entry.Name,
                                     nodes[0],
                                     nodes[1],
                                     nodes[2],
                                     nodes[3],
                                     nodes[4],
                                     MakeBoyleParams(op_amp.GetParameters()));
        }
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
