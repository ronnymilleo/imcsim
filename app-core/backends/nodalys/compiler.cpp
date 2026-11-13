/**
 * @file    compiler.cpp
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into an autonomous `Nodalys::Circuit`.
 */

#include "backends/nodalys/compiler.h"

#include "backends/nodalys/model_parser.h"
#include "backends/nodalys/parameter_conversion.h"
#include "circuit/circuit_currents.h"
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
#include <cmath>
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

std::expected<std::vector<ProbeSpec>, std::string> PlanProbes(const Circuit &circuit) {
    std::vector<ProbeSpec> probes;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsCurrentControlled(entry.Part->GetType())) {
            continue;
        }
        const auto &source = static_cast<const ControlledSource &>(*entry.Part);
        const auto target = FindControllingCurrent(circuit, source.GetControllingCurrent());
        if (!target) {
            return std::unexpected(target.error());
        }
        probes.push_back(*target);
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
                            part.GetName(), name));
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

        return m_Builder->Build();
    }

private:
    const Circuit &m_Circuit;
    std::unique_ptr<Nodalys::CircuitBuilder> m_Builder;

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

    std::expected<void, std::string> AddCircuit(const Circuit &circuit, const std::vector<int> &node_map,
                                                const Parameters &parameters, const std::string &prefix = "") {
        const auto probes = PlanProbes(circuit);
        if (!probes) {
            return std::unexpected(probes.error());
        }

        // entry_index -> terminal_index -> probe_node
        std::map<int, std::map<int, int>> probe_nodes;
        for (const ProbeSpec &probe : *probes) {
            const CircuitEntry &target = circuit.GetEntries()[static_cast<std::size_t>(probe.Entry)];
            const int original_node =
                node_map[static_cast<std::size_t>(target.Nodes[static_cast<std::size_t>(probe.Terminal)])];
            const int probe_node = m_Builder->AddNode();
            const std::string probe_name = prefix.empty()
                                               ? std::format("__probe_{}_{}", probe.Entry, probe.Terminal)
                                               : std::format("__probe_{}_{}_{}", prefix, probe.Entry, probe.Terminal);
            m_Builder->AddCurrentProbe(probe_name, original_node, probe_node);
            probe_nodes[probe.Entry][probe.Terminal] = probe_node;
        }

        for (std::size_t index = 0; index < circuit.GetEntries().size(); ++index) {
            if (const auto result = AddEntry(circuit.GetEntries()[index], circuit, static_cast<int>(index), node_map,
                                             parameters, probe_nodes, prefix);
                !result) {
                return std::unexpected(result.error());
            }
        }
        return {};
    }

    std::expected<void, std::string> AddEntry(const CircuitEntry &entry, const Circuit &owner, const int entry_index,
                                              const std::vector<int> &node_map, const Parameters &parameters,
                                              const std::map<int, std::map<int, int>> &probe_map,
                                              const std::string &prefix = "") {
        const Component &part = *entry.Part;
        const ComponentType type = part.GetType();
        const std::string name = prefix.empty() ? part.GetName() : std::format("{}.{}", prefix, part.GetName());

        std::vector<int> nodes;
        nodes.reserve(entry.Nodes.size());
        for (const int local_node : entry.Nodes) {
            nodes.push_back(node_map[static_cast<std::size_t>(local_node)]);
        }

        if (const auto probes = probe_map.find(entry_index); probes != probe_map.end()) {
            for (const auto &[terminal, probe_node] : probes->second) {
                nodes[static_cast<std::size_t>(terminal)] = probe_node;
            }
        }

        switch (type) {
        case ComponentType::GROUND:
        case ComponentType::NET_LABEL:
        case ComponentType::PIN:
            return {};

        case ComponentType::RESISTOR: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            m_Builder->AddResistor(name, nodes[0], nodes[1], *value);
            return {};
        }

        case ComponentType::CAPACITOR: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            const std::optional<double> initial = part.GetInitialCondition();
            m_Builder->AddCapacitor(name, nodes[0], nodes[1], *value, initial.value_or(0.0));
            return {};
        }

        case ComponentType::INDUCTOR: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            const std::optional<double> initial = part.GetInitialCondition();
            m_Builder->AddInductor(name, nodes[0], nodes[1], *value, initial.value_or(0.0));
            return {};
        }

        case ComponentType::SWITCH:
        case ComponentType::PUSH_BUTTON: {
            const auto &contact = static_cast<const Switch &>(part);
            m_Builder->AddResistor(prefix.empty() ? std::format("R-{}", part.GetName())
                                                  : std::format("{}.R-{}", prefix, part.GetName()),
                                   nodes[0], nodes[1], contact.GetResistance());
            return {};
        }

        case ComponentType::VCC: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            m_Builder->AddVdc(name, nodes[0], 0, *value);
            return {};
        }

        case ComponentType::VOLTAGE_SOURCE: {
            const auto &source = static_cast<const Source &>(part);
            const double ac_magnitude =
                source.GetSourceType() == Source::SourceType::AC ? source.GetAC().Magnitude : 0.0;
            switch (source.GetSourceType()) {
            case Source::SourceType::DC:
                m_Builder->AddVdc(name, nodes[0], nodes[1], source.GetValue(), ac_magnitude);
                break;
            case Source::SourceType::AC:
                m_Builder->AddVsine(name, nodes[0], nodes[1], ToNodalys(source.GetAC()), ac_magnitude);
                break;
            case Source::SourceType::PULSE:
                m_Builder->AddVpulse(name, nodes[0], nodes[1], ToNodalys(source.GetPulse()), ac_magnitude);
                break;
            case Source::SourceType::EXP:
                m_Builder->AddVexp(name, nodes[0], nodes[1], ToNodalys(source.GetEXP()), ac_magnitude);
                break;
            case Source::SourceType::PWL:
                m_Builder->AddVpwl(name, nodes[0], nodes[1], ToNodalys(source.GetPWL()), ac_magnitude);
                break;
            case Source::SourceType::SFFM:
                m_Builder->AddVsffm(name, nodes[0], nodes[1], ToNodalys(source.GetSFFM()), ac_magnitude);
                break;
            case Source::SourceType::AM:
                m_Builder->AddVam(name, nodes[0], nodes[1], ToNodalys(source.GetAM()), ac_magnitude);
                break;
            // Outside a transient, which refuses them, a noise or random source holds the level ngspice gives it
            case Source::SourceType::NOISE:
                m_Builder->AddVdc(name, nodes[0], nodes[1], source.GetNoise().Offset, ac_magnitude);
                break;
            case Source::SourceType::RANDOM:
            default:
                m_Builder->AddVdc(name, nodes[0], nodes[1], source.GetRandom().Second, ac_magnitude);
                break;
            }
            return {};
        }

        case ComponentType::CURRENT_SOURCE: {
            const auto &source = static_cast<const Source &>(part);
            const double ac_magnitude =
                source.GetSourceType() == Source::SourceType::AC ? source.GetAC().Magnitude : 0.0;
            switch (source.GetSourceType()) {
            case Source::SourceType::DC:
                m_Builder->AddIdc(name, nodes[0], nodes[1], source.GetValue(), ac_magnitude);
                break;
            case Source::SourceType::AC:
                m_Builder->AddIsine(name, nodes[0], nodes[1], ToNodalys(source.GetAC()), ac_magnitude);
                break;
            case Source::SourceType::PULSE:
                m_Builder->AddIpulse(name, nodes[0], nodes[1], ToNodalys(source.GetPulse()), ac_magnitude);
                break;
            case Source::SourceType::EXP:
                m_Builder->AddIexp(name, nodes[0], nodes[1], ToNodalys(source.GetEXP()), ac_magnitude);
                break;
            case Source::SourceType::PWL:
                m_Builder->AddIpwl(name, nodes[0], nodes[1], ToNodalys(source.GetPWL()), ac_magnitude);
                break;
            case Source::SourceType::SFFM:
                m_Builder->AddIsffm(name, nodes[0], nodes[1], ToNodalys(source.GetSFFM()), ac_magnitude);
                break;
            case Source::SourceType::AM:
                m_Builder->AddIam(name, nodes[0], nodes[1], ToNodalys(source.GetAM()), ac_magnitude);
                break;
            // Outside a transient, which refuses them, a noise or random source holds the level ngspice gives it
            case Source::SourceType::NOISE:
                m_Builder->AddIdc(name, nodes[0], nodes[1], source.GetNoise().Offset, ac_magnitude);
                break;
            case Source::SourceType::RANDOM:
            default:
                m_Builder->AddIdc(name, nodes[0], nodes[1], source.GetRandom().Second, ac_magnitude);
                break;
            }
            return {};
        }

        case ComponentType::CLOCK: {
            const auto &clock = static_cast<const Clock &>(part);
            m_Builder->AddClock(name, nodes[0], 0, ToNodalys(clock.GetParameters()));
            return {};
        }

        case ComponentType::LOGIC_INPUT: {
            const auto &input = static_cast<const LogicInput &>(part);
            m_Builder->AddLogicInput(name, nodes[0], 0, input.IsHigh(), input.GetParameters().LogicLevel, 0.0);
            return {};
        }

        case ComponentType::POTENTIOMETER: {
            const auto &pot = static_cast<const Potentiometer &>(part);
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            m_Builder->AddPotentiometer(name, nodes[0], nodes[2], nodes[1], *value, pot.GetWiper());
            return {};
        }

        case ComponentType::SUBCIRCUIT: {
            const auto &instance = static_cast<const SubcircuitInstance &>(part);
            const std::shared_ptr<const SubcircuitDefinition> definition =
                m_Circuit.FindSubcircuit(instance.GetDefinitionName());
            if (!definition) {
                return std::unexpected(std::format("The subcircuit {} is not defined", instance.GetDefinitionName()));
            }
            if (definition->Spice) {
                return std::unexpected(std::format(
                    "Nodalys cannot simulate the subcircuit {}, which is kept as SPICE text", definition->Name));
            }
            const std::set<int> used = UsedNodes(definition->Body);
            std::vector<int> body_map(static_cast<std::size_t>(definition->Body.GetNodeCount()), 0);
            for (std::size_t pin = 0; pin < definition->Pins.size() && pin < nodes.size(); ++pin) {
                body_map[static_cast<std::size_t>(definition->Pins[pin].Node)] = nodes[pin];
            }
            for (const int body_node : used) {
                if (body_map[static_cast<std::size_t>(body_node)] == 0 &&
                    std::ranges::none_of(definition->Pins,
                                         [body_node](const SubcircuitPin &p) { return p.Node == body_node; })) {
                    body_map[static_cast<std::size_t>(body_node)] = m_Builder->AddNode();
                }
            }
            const std::string sub_prefix =
                prefix.empty() ? instance.GetName() : std::format("{}.{}", prefix, instance.GetName());
            return AddCircuit(definition->Body, body_map, InstanceParameters(instance, *definition), sub_prefix);
        }

        case ComponentType::DIODE:
        case ComponentType::ZENER_DIODE:
        case ComponentType::LED: {
            const auto &diode = static_cast<const Diode &>(part);
            const auto resolved = ResolveParameters(m_Circuit, diode);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            m_Builder->AddDiode(name, nodes[0], nodes[1], ToNodalys(*resolved));
            return {};
        }

        case ComponentType::NPN:
        case ComponentType::PNP: {
            const auto &bjt = static_cast<const BJT &>(part);
            const auto resolved = ResolveParameters(m_Circuit, bjt);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            m_Builder->AddBjt(name, nodes[0], nodes[1], nodes[2], type != ComponentType::PNP, ToNodalys(*resolved));
            return {};
        }

        case ComponentType::NMOS:
        case ComponentType::PMOS: {
            const auto &mosfet = static_cast<const MOSFET &>(part);
            const auto resolved = ResolveParameters(m_Circuit, mosfet);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const int body = nodes.size() > 3 ? nodes[3] : nodes[2];
            m_Builder->AddMosfet(name, nodes[0], nodes[1], nodes[2], body, type != ComponentType::PMOS,
                                 ToNodalys(*resolved));
            return {};
        }

        case ComponentType::V_SWITCH: {
            const auto &controlled = static_cast<const VoltageControlledSwitch &>(part);
            m_Builder->AddVoltageControlledSwitch(name, nodes[0], nodes[1], nodes[2], nodes[3],
                                                  ToNodalys(controlled.GetParameters()));
            return {};
        }

        case ComponentType::FUSE: {
            m_Builder->AddFuse(name, nodes[0], nodes[1], ToNodalys(static_cast<const Fuse &>(part).GetParameters()));
            return {};
        }

        case ComponentType::RELAY: {
            const auto &relay = static_cast<const Relay &>(part);
            const RelayParameters &figures = relay.GetParameters();
            m_Builder->AddRelay(name, nodes[2], nodes[3], nodes[0], nodes[1], figures.CoilResistance,
                                figures.CoilInductance, ToNodalys(relay.GetContactParameters()));
            return {};
        }

        case ComponentType::TRANSFORMER: {
            const auto &transformer = static_cast<const Transformer &>(part);
            const TransformerParameters &figures = transformer.GetParameters();
            const double l1 = transformer.GetValue();
            const double l2 = transformer.GetSecondaryInductance();
            m_Builder->AddTransformer(name, nodes[0], nodes[1], nodes[2], nodes[3], l1, l2, figures.Coupling,
                                      figures.WindingResistance);
            return {};
        }

        case ComponentType::VCVS:
        case ComponentType::VCCS:
        case ComponentType::CCCS:
        case ComponentType::CCVS: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            const auto &source = static_cast<const ControlledSource &>(part);
            switch (type) {
            case ComponentType::VCVS:
                m_Builder->AddVcvs(name, nodes[0], nodes[1], nodes[2], nodes[3], *value);
                break;
            case ComponentType::VCCS:
                m_Builder->AddVccs(name, nodes[0], nodes[1], nodes[2], nodes[3], *value);
                break;
            case ComponentType::CCCS:
            case ComponentType::CCVS: {
                const auto target = FindControllingCurrent(owner, source.GetControllingCurrent());
                if (!target) {
                    return std::unexpected(target.error());
                }
                const std::string probe_name =
                    prefix.empty() ? std::format("__probe_{}_{}", target->Entry, target->Terminal)
                                   : std::format("__probe_{}_{}_{}", prefix, target->Entry, target->Terminal);
                if (type == ComponentType::CCCS) {
                    m_Builder->AddCccs(name, nodes[0], nodes[1], probe_name, *value);
                } else {
                    m_Builder->AddCcvs(name, nodes[0], nodes[1], probe_name, *value);
                }
                break;
            }
            default:
                break;
            }
            return {};
        }

        case ComponentType::NOT: {
            const auto &logic = static_cast<const LogicPart &>(part);
            m_Builder->AddInverter(name, nodes[1], nodes[0], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::BUFFER: {
            const auto &logic = static_cast<const LogicPart &>(part);
            m_Builder->AddLogicBuffer(name, nodes[1], nodes[0], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::AND:
        case ComponentType::NAND:
        case ComponentType::OR:
        case ComponentType::NOR:
        case ComponentType::XOR:
        case ComponentType::XNOR: {
            const auto &logic = static_cast<const LogicPart &>(part);
            const int input_count = logic.GetInputCount();
            std::vector<Nodalys::NodeId> inputs;
            inputs.reserve(static_cast<std::size_t>(input_count));
            for (int i = 0; i < input_count; ++i) {
                inputs.push_back(nodes[static_cast<std::size_t>(i)]);
            }
            const int out_node = nodes[static_cast<std::size_t>(input_count)];
            Nodalys::LogicGateType gate_type = Nodalys::LogicGateType::AND;
            switch (type) {
            case ComponentType::AND:
                gate_type = Nodalys::LogicGateType::AND;
                break;
            case ComponentType::NAND:
                gate_type = Nodalys::LogicGateType::NAND;
                break;
            case ComponentType::OR:
                gate_type = Nodalys::LogicGateType::OR;
                break;
            case ComponentType::NOR:
                gate_type = Nodalys::LogicGateType::NOR;
                break;
            case ComponentType::XOR:
                gate_type = Nodalys::LogicGateType::XOR;
                break;
            case ComponentType::XNOR:
                gate_type = Nodalys::LogicGateType::XNOR;
                break;
            default:
                break;
            }
            m_Builder->AddLogicGate(name, gate_type, out_node, std::move(inputs), MakeLogicParams(logic));
            return {};
        }

        case ComponentType::D_FLIP_FLOP: {
            const auto &logic = static_cast<const LogicPart &>(part);
            m_Builder->AddDFlipFlop(name, nodes[4], nodes[5], nodes[0], nodes[1], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::SR_FLIP_FLOP: {
            const auto &logic = static_cast<const LogicPart &>(part);
            const int node_not_r = m_Builder->AddNode();
            const int node_and = m_Builder->AddNode();
            const int node_d = m_Builder->AddNode();
            m_Builder->AddInverter(name + ".NOT_R", node_not_r, nodes[1], MakeLogicParams(logic));
            m_Builder->AddLogicGate(name + ".AND", Nodalys::LogicGateType::AND, node_and, {node_not_r, nodes[5]},
                                    MakeLogicParams(logic));
            m_Builder->AddLogicGate(name + ".OR", Nodalys::LogicGateType::OR, node_d, {nodes[0], node_and},
                                    MakeLogicParams(logic));
            m_Builder->AddDFlipFlop(name, nodes[5], nodes[6], node_d, nodes[2], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::T_FLIP_FLOP: {
            const auto &logic = static_cast<const LogicPart &>(part);
            const int internal_d = m_Builder->AddNode();
            m_Builder->AddLogicGate(name + ".XOR", Nodalys::LogicGateType::XOR, internal_d, {nodes[0], nodes[4]},
                                    MakeLogicParams(logic));
            m_Builder->AddDFlipFlop(name, nodes[4], nodes[5], internal_d, nodes[1], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::JK_FLIP_FLOP: {
            const auto &logic = static_cast<const LogicPart &>(part);
            const int node_not_k = m_Builder->AddNode();
            const int node_and1 = m_Builder->AddNode();
            const int node_and2 = m_Builder->AddNode();
            const int node_d = m_Builder->AddNode();
            m_Builder->AddInverter(name + ".NOT_K", node_not_k, nodes[1], MakeLogicParams(logic));
            m_Builder->AddLogicGate(name + ".AND1", Nodalys::LogicGateType::AND, node_and1, {nodes[0], nodes[6]},
                                    MakeLogicParams(logic));
            m_Builder->AddLogicGate(name + ".AND2", Nodalys::LogicGateType::AND, node_and2, {node_not_k, nodes[5]},
                                    MakeLogicParams(logic));
            m_Builder->AddLogicGate(name + ".OR", Nodalys::LogicGateType::OR, node_d, {node_and1, node_and2},
                                    MakeLogicParams(logic));
            m_Builder->AddDFlipFlop(name, nodes[5], nodes[6], node_d, nodes[2], MakeLogicParams(logic));
            return {};
        }

        case ComponentType::OP_AMP: {
            const auto &op_amp = static_cast<const OpAmp &>(part);
            if (op_amp.IsIdeal()) {
                m_Builder->AddIdealOpAmp(name, nodes[0], nodes[1], nodes[2], nodes[3], nodes[4],
                                         op_amp.GetIdealBandwidth());
            } else {
                m_Builder->AddBoyleOpAmp(name, nodes[0], nodes[1], nodes[2], nodes[3], nodes[4],
                                         MakeBoyleParams(op_amp.GetParameters()));
            }
            return {};
        }

        default:
            return std::unexpected(
                std::format("Nodalys has no device for {} (a {} part) yet", part.GetName(), GetTypeName(type)));
        }
    }
};

} // namespace

std::expected<Nodalys::Circuit, std::string> CompileCircuit(const Circuit &circuit) {
    return CircuitCompiler(circuit).Compile();
}

} // namespace Core
