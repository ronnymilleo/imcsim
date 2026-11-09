/**
 * @file    compiler.cpp
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into the MNA equation system and `Device` list
 *          of the native solver.
 */

#include "native/compiler.h"

#include "components/bjt.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/logic_input.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/source.h"
#include "components/subcircuit.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/voltage_controlled_switch.h"
#include "model/subcircuit.h"
#include "native/model_parser.h"
#include "native/nonlinear.h"
#include "native/waveforms.h"
#include "spice/spice_library.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace Core::Native {

namespace {

using Parameters = std::map<std::string, double>;

/**
 * @struct  ProbeSpec
 * @brief   Identifies a component terminal whose current is sensed by a current-controlled source (CCCS or CCVS).
 * @details A $0\text{ V}$ voltage source ("ammeter probe") is inserted in series with that terminal so its current
 *          becomes a branch unknown in the MNA system, matching how SPICE netlists insert `Vprobe` elements.
 */
struct ProbeSpec {
    int Entry;
    int Terminal;
};

/**
 * @brief   Finds which component entry and terminal a current name (such as `"R1"` or `"Q1.C"`) refers to.
 */
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
 * @brief   Collects all terminals that need a $0\text{ V}$ current-sensing probe for CCCS/CCVS elements.
 */
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

/**
 * @brief   Returns all non-ground node numbers (`> 0`) referenced by entries in a circuit.
 */
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

/**
 * @brief   Resolves a component's numeric value, substituting `{parameter}` placeholders when inside a subcircuit.
 */
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
    return std::unexpected(std::format("{} uses the value expression {}, which the native backend cannot evaluate",
                                       part.GetName(), *expression));
}

/**
 * @class   Builder
 * @brief   Single-pass compiler: allocates node and branch unknowns on the fly as devices are instantiated.
 * @details
 * Unlike a two-pass compiler that counts nodes/branches first and builds devices second, `Builder` allocates
 * unknown indices directly via `AllocateInternalNode()` and `AllocateBranch()` as each component is added:
 * - Ground (circuit node `0`) always has unknown index `-1`.
 * - Top-level circuit nodes `1 .. CircuitNodeCount - 1` occupy unknown indices `0 .. CircuitNodeCount - 2`.
 * - Internal nodes and branch currents are appended to `m_IsNodeUnknown` as they are requested.
 * - At the end of `Build()`, `MnaSystem` is constructed once with the complete `m_IsNodeUnknown` descriptor.
 */
class Builder {
public:
    explicit Builder(const Circuit &circuit) : m_Circuit(circuit) {}

    std::expected<CompiledCircuit, std::string> Build() {
        const int circuit_nodes = m_Circuit.GetNodeCount();
        const int top_node_unknowns = std::max(0, circuit_nodes - 1);
        m_IsNodeUnknown.assign(static_cast<std::size_t>(top_node_unknowns), true);
        m_DynamicUnknown.assign(static_cast<std::size_t>(top_node_unknowns), false);

        // Map top-level circuit nodes: node 0 -> -1 (ground), node k -> k - 1 (unknown index)
        std::vector<int> top_node_map(static_cast<std::size_t>(circuit_nodes), -1);
        for (int node = 1; node < circuit_nodes; ++node) {
            top_node_map[static_cast<std::size_t>(node)] = node - 1;
        }

        if (const auto result = AddCircuit(m_Circuit, top_node_map, {}); !result) {
            return std::unexpected(result.error());
        }

        m_Result.CircuitNodeCount = circuit_nodes;
        m_Result.CompiledNodeCount = static_cast<int>(std::ranges::count(m_IsNodeUnknown, true)) + 1;
        m_Result.System = std::make_unique<MnaSystem>(std::move(m_IsNodeUnknown));
        m_Result.DynamicNode = std::move(m_DynamicUnknown);
        return std::move(m_Result);
    }

private:
    const Circuit &m_Circuit;
    CompiledCircuit m_Result;
    /// True for node-voltage unknowns, false for branch-current unknowns.
    std::vector<bool> m_IsNodeUnknown;
    /// True for node-voltage unknowns connected to capacitive energy-storage elements (watched by LTE control).
    std::vector<bool> m_DynamicUnknown;

    /// Allocates a new internal node-voltage unknown and returns its MNA index.
    int AllocateInternalNode() {
        const int index = static_cast<int>(m_IsNodeUnknown.size());
        m_IsNodeUnknown.push_back(true);
        m_DynamicUnknown.push_back(false);
        return index;
    }

    /// Allocates a new branch-current unknown and returns its MNA index.
    int AllocateBranch() {
        const int index = static_cast<int>(m_IsNodeUnknown.size());
        m_IsNodeUnknown.push_back(false);
        m_DynamicUnknown.push_back(false);
        return index;
    }

    /// Marks a node unknown as carrying a capacitive state so transient LTE control monitors it.
    void MarkDynamic(const int node_unknown) {
        if (node_unknown >= 0 && static_cast<std::size_t>(node_unknown) < m_DynamicUnknown.size()) {
            m_DynamicUnknown[static_cast<std::size_t>(node_unknown)] = true;
        }
    }

    Device *AddDevice(std::unique_ptr<Device> device) {
        Device *raw = device.get();
        m_Result.Devices.push_back(std::move(device));
        return raw;
    }

    /// Registers a single-current component (resistor, capacitor, inductor, diode, source) in the results list.
    void AddSingleCurrents(const Component &part, const Device *device) {
        for (const std::string &name : GetCurrentNames(part)) {
            m_Result.Currents.push_back({name, {{device, 0}}});
        }
    }

    /// Registers a multi-terminal component (BJT or MOSFET) whose terminals map 1-to-1 to `GetCurrentNames()`.
    void AddTerminalCurrents(const Component &part, const Device *device) {
        const std::vector<std::string> names = GetCurrentNames(part);
        for (std::size_t index = 0; index < names.size(); ++index) {
            m_Result.Currents.push_back({names[index], {{device, static_cast<int>(index)}}});
        }
    }

    std::expected<void, std::string> AddEntry(const CircuitEntry &entry, const Circuit &owner, const int entry_index,
                                              const std::vector<int> &node_map, const Parameters &parameters,
                                              const std::map<int, std::map<int, std::pair<int, int>>> &probe_map) {
        const Component &part = *entry.Part;
        const ComponentType type = part.GetType();

        // Translate local circuit node numbers into MNA unknown indices (-1 for ground)
        std::vector<int> nodes;
        nodes.reserve(entry.Nodes.size());
        for (const int local_node : entry.Nodes) {
            nodes.push_back(node_map[static_cast<std::size_t>(local_node)]);
        }

        // If a current-controlled source probes a terminal of this component, redirect that terminal to the probe node
        if (const auto probes = probe_map.find(entry_index); probes != probe_map.end()) {
            for (const auto &[terminal, probe_info] : probes->second) {
                nodes[static_cast<std::size_t>(terminal)] = probe_info.first;
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
            Device *device = AddDevice(std::make_unique<ResistorDevice>(nodes[0], nodes[1], *value));
            m_Result.ElementsByName[part.GetName()] = device;
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::CAPACITOR: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            const std::optional<double> initial = part.GetInitialCondition();
            MarkDynamic(nodes[0]);
            MarkDynamic(nodes[1]);
            Device *device = AddDevice(std::make_unique<CapacitorDevice>(
                nodes[0], nodes[1], *value, initial.value_or(0.0), initial.has_value()));
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::INDUCTOR: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            const std::optional<double> initial = part.GetInitialCondition();
            // Inductor state is the branch current; marking its nodes for voltage LTE would falsely reject steps on
            // stiff switched-inductor circuits where voltage jumps instantaneously while current is continuous.
            Device *device = AddDevice(std::make_unique<InductorDevice>(
                nodes[0], nodes[1], AllocateBranch(), *value, initial.value_or(0.0), initial.has_value()));
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::SWITCH:
        case ComponentType::PUSH_BUTTON: {
            const auto &contact = static_cast<const Switch &>(part);
            Device *device = AddDevice(std::make_unique<ResistorDevice>(nodes[0], nodes[1], contact.GetResistance()));
            m_Result.ElementsByName[std::format("R-{}", part.GetName())] = device;
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::VCC: {
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            Device *raw = AddDevice(std::make_unique<VoltageSourceDevice>(
                nodes[0], -1, AllocateBranch(), *value,
                [](const double dc, const TimeContext &) { return dc; }));
            m_Result.SourcesByName[part.GetName()] = raw;
            m_Result.ElementsByName[part.GetName()] = raw;
            AddSingleCurrents(part, raw);
            return {};
        }

        case ComponentType::VOLTAGE_SOURCE: {
            const auto &source = static_cast<const Source &>(part);
            Device *raw = AddDevice(std::make_unique<VoltageSourceDevice>(
                nodes[0], nodes[1], AllocateBranch(), SourceDcValue(source), MakeSourceWaveform(source)));
            m_Result.SourcesByName[part.GetName()] = raw;
            m_Result.ElementsByName[part.GetName()] = raw;
            AddSingleCurrents(part, raw);
            return {};
        }

        case ComponentType::CURRENT_SOURCE: {
            const auto &source = static_cast<const Source &>(part);
            Device *raw = AddDevice(std::make_unique<CurrentSourceDevice>(
                nodes[0], nodes[1], SourceDcValue(source), MakeSourceWaveform(source)));
            m_Result.SourcesByName[part.GetName()] = raw;
            m_Result.ElementsByName[part.GetName()] = raw;
            AddSingleCurrents(part, raw);
            return {};
        }

        case ComponentType::CLOCK: {
            const auto &clock = static_cast<const Clock &>(part);
            Device *raw = AddDevice(std::make_unique<VoltageSourceDevice>(
                nodes[0], -1, AllocateBranch(), 0.0, MakeClockWaveform(clock.GetParameters())));
            m_Result.SourcesByName[part.GetName()] = raw;
            m_Result.ElementsByName[part.GetName()] = raw;
            AddSingleCurrents(part, raw);
            return {};
        }

        case ComponentType::LOGIC_INPUT: {
            const auto &input = static_cast<const LogicInput &>(part);
            Device *raw = AddDevice(std::make_unique<VoltageSourceDevice>(
                nodes[0], -1, AllocateBranch(), input.GetVoltage(),
                [](const double dc, const TimeContext &) { return dc; }));
            m_Result.SourcesByName[part.GetName()] = raw;
            m_Result.ElementsByName[part.GetName()] = raw;
            AddSingleCurrents(part, raw);
            return {};
        }

        case ComponentType::POTENTIOMETER: {
            const auto &pot = static_cast<const Potentiometer &>(part);
            const auto value = EffectiveValue(part, parameters);
            if (!value) {
                return std::unexpected(value.error());
            }
            std::unique_ptr<Component> copy = pot.Clone();
            copy->SetValue(*value);
            const auto &resolved = static_cast<const Potentiometer &>(*copy);
            // Terminal 0 = end A, Terminal 1 = end B, Terminal 2 = wiper W:
            //   [nodes[0] (A)] --- R_first --- [nodes[2] (W)] --- R_second --- [nodes[1] (B)]
            Device *first = AddDevice(
                std::make_unique<ResistorDevice>(nodes[0], nodes[2], resolved.GetFirstTrackResistance()));
            Device *second = AddDevice(
                std::make_unique<ResistorDevice>(nodes[2], nodes[1], resolved.GetSecondTrackResistance()));
            m_Result.ElementsByName[std::format("R-{}a", part.GetName())] = first;
            m_Result.ElementsByName[std::format("R-{}b", part.GetName())] = second;
            m_Result.Currents.push_back({std::format("{}.A", part.GetName()), {{first, 0}}});
            m_Result.Currents.push_back({std::format("{}.B", part.GetName()), {{second, 1}}});
            m_Result.Currents.push_back({std::format("{}.W", part.GetName()), {{first, 1}, {second, 0}}});
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
                return std::unexpected(
                    std::format("The native backend cannot simulate the subcircuit {}, which is kept as SPICE text",
                                definition->Name));
            }
            // Map subcircuit pins to the instance's external unknown indices, and allocate fresh internal node
            // unknowns for every other non-ground node used inside the subcircuit body.
            const std::set<int> used = UsedNodes(definition->Body);
            std::vector<int> body_map(static_cast<std::size_t>(definition->Body.GetNodeCount()), -1);
            for (std::size_t pin = 0; pin < definition->Pins.size() && pin < nodes.size(); ++pin) {
                body_map[static_cast<std::size_t>(definition->Pins[pin].Node)] = nodes[pin];
            }
            for (const int body_node : used) {
                if (body_map[static_cast<std::size_t>(body_node)] < 0 &&
                    std::ranges::none_of(definition->Pins,
                                         [body_node](const SubcircuitPin &p) { return p.Node == body_node; })) {
                    body_map[static_cast<std::size_t>(body_node)] = AllocateInternalNode();
                }
            }
            return AddCircuit(definition->Body, body_map, InstanceParameters(instance, *definition));
        }

        case ComponentType::DIODE:
        case ComponentType::ZENER_DIODE:
        case ComponentType::LED: {
            const auto &diode = static_cast<const Diode &>(part);
            const auto resolved = ResolveParameters(m_Circuit, diode);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const int internal = resolved->SeriesResistance > 0.0 ? AllocateInternalNode() : -1;
            Device *device = AddDevice(std::make_unique<DiodeDevice>(nodes[0], nodes[1], internal, *resolved));
            m_Result.ElementsByName[part.GetName()] = device;
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::NPN:
        case ComponentType::PNP: {
            const auto &bjt = static_cast<const BJT &>(part);
            const auto resolved = ResolveParameters(m_Circuit, bjt);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const BJTParameters &p = *resolved;
            const int internal_base = p.BaseResistance > 0.0 ? AllocateInternalNode() : -1;
            const int internal_collector = p.CollectorResistance > 0.0 ? AllocateInternalNode() : -1;
            const int internal_emitter = p.EmitterResistance > 0.0 ? AllocateInternalNode() : -1;
            Device *device = AddDevice(std::make_unique<BjtDevice>(
                nodes[0], nodes[1], nodes[2], internal_collector, internal_base, internal_emitter, p,
                type == ComponentType::PNP));
            AddTerminalCurrents(part, device);
            return {};
        }

        case ComponentType::NMOS:
        case ComponentType::PMOS: {
            const auto &mosfet = static_cast<const MOSFET &>(part);
            const auto resolved = ResolveParameters(m_Circuit, mosfet);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const MOSFETParameters &p = *resolved;
            const int internal_drain = p.DrainResistance > 0.0 ? AllocateInternalNode() : -1;
            const int internal_source = p.SourceResistance > 0.0 ? AllocateInternalNode() : -1;
            Device *device = AddDevice(std::make_unique<MosfetDevice>(
                nodes[0], nodes[1], nodes[2], internal_drain, internal_source, p, type == ComponentType::PMOS));
            AddTerminalCurrents(part, device);
            return {};
        }

        case ComponentType::V_SWITCH: {
            const auto &controlled = static_cast<const VoltageControlledSwitch &>(part);
            Device *device = AddDevice(std::make_unique<SwitchDevice>(
                nodes[0], nodes[1], nodes[2], nodes[3], controlled.GetParameters()));
            m_Result.ElementsByName[part.GetName()] = device;
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::RELAY: {
            const auto &relay = static_cast<const Relay &>(part);
            const RelayParameters &figures = relay.GetParameters();
            // Relay topology:
            //   Coil:    [nodes[2]] --- R_coil --- [middle] --- L_coil --- [nodes[3]]
            //   Contact: [nodes[0]] --- Switch(controlled by V(nodes[2], middle)) --- [nodes[1]]
            const int middle = AllocateInternalNode();
            Device *coil_resistance =
                AddDevice(std::make_unique<ResistorDevice>(nodes[2], middle, figures.CoilResistance));
            AddDevice(std::make_unique<InductorDevice>(
                middle, nodes[3], AllocateBranch(), figures.CoilInductance, 0.0, false));
            Device *contact = AddDevice(
                std::make_unique<SwitchDevice>(nodes[0], nodes[1], nodes[2], middle, relay.GetContactParameters()));
            m_Result.Currents.push_back({std::format("{}.S", part.GetName()), {{contact, 0}}});
            m_Result.Currents.push_back({std::format("{}.C", part.GetName()), {{coil_resistance, 0}}});
            return {};
        }

        case ComponentType::TRANSFORMER: {
            const auto &transformer = static_cast<const Transformer &>(part);
            const TransformerParameters &figures = transformer.GetParameters();
            const double l1 = transformer.GetValue();
            const double l2 = transformer.GetSecondaryInductance();
            const double mutual = figures.Coupling * std::sqrt(l1 * l2);
            // Each winding has a series winding resistance before the coupled inductance:
            //   Primary:   [nodes[0]] --- R_w --- [pri_mid] --- L1 --- [nodes[1]]
            //   Secondary: [nodes[2]] --- R_w --- [sec_mid] --- L2 --- [nodes[3]]
            const int pri_mid = AllocateInternalNode();
            const int sec_mid = AllocateInternalNode();
            Device *primary =
                AddDevice(std::make_unique<ResistorDevice>(nodes[0], pri_mid, figures.WindingResistance));
            Device *secondary =
                AddDevice(std::make_unique<ResistorDevice>(nodes[2], sec_mid, figures.WindingResistance));
            AddDevice(std::make_unique<CoupledInductorDevice>(
                pri_mid, nodes[1], sec_mid, nodes[3], AllocateBranch(), AllocateBranch(), l1, l2, mutual));
            m_Result.Currents.push_back({std::format("{}.P", part.GetName()), {{primary, 0}}});
            m_Result.Currents.push_back({std::format("{}.S", part.GetName()), {{secondary, 0}}});
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
            Device *device = nullptr;
            switch (type) {
            case ComponentType::VCVS:
                device = AddDevice(
                    std::make_unique<VcvsDevice>(nodes[0], nodes[1], nodes[2], nodes[3], AllocateBranch(), *value));
                break;
            case ComponentType::VCCS:
                device = AddDevice(std::make_unique<VccsDevice>(nodes[0], nodes[1], nodes[2], nodes[3], *value));
                break;
            case ComponentType::CCCS:
            case ComponentType::CCVS: {
                const auto target = FindControllingCurrent(owner, source.GetControllingCurrent());
                if (!target) {
                    return std::unexpected(target.error());
                }
                const auto probe = probe_map.find(target->Entry);
                if (probe == probe_map.end() || !probe->second.contains(target->Terminal)) {
                    return std::unexpected(std::format("{} follows {}, which has no probe", part.GetName(),
                                                       source.GetControllingCurrent()));
                }
                const int control_branch = probe->second.at(target->Terminal).second;
                if (type == ComponentType::CCCS) {
                    device = AddDevice(std::make_unique<CccsDevice>(nodes[0], nodes[1], control_branch, *value));
                } else {
                    device = AddDevice(
                        std::make_unique<CcvsDevice>(nodes[0], nodes[1], AllocateBranch(), control_branch, *value));
                }
                break;
            }
            default:
                break;
            }
            AddSingleCurrents(part, device);
            return {};
        }

        case ComponentType::OP_AMP:
            return AddOpAmp(static_cast<const OpAmp &>(part), nodes);

        default:
            return std::unexpected(std::format("The native backend has no device for {} (a {} part) yet",
                                               part.GetName(), GetTypeName(type)));
        }
    }

    static double ConstantValue(const double dc, const TimeContext &) { return dc; }

    /**
     * @brief   Builds the ideal op-amp macromodel, matching `FormatIdealOpAmp()` in `op_amp.cpp`.
     * @details
     * ```text
     *   in+ (nodes[1]) / in- (nodes[2])
     *        |
     *        v
     *   [G: VCCS] ---> (n_stage) ---+--- [R: GainStage] --- GND
     *                               +--- [C: PoleCap]   --- GND
     *                               +--- [D_P] ------------> V+ (nodes[3])
     *                               +--- [D_N] <------------ V- (nodes[4])
     *                               |
     *                               +--> [E: Unity VCVS] --> out (nodes[0])
     * ```
     */
    std::expected<void, std::string> AddIdealOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes) {
        const IdealOpAmpValues figures = ComputeIdealOpAmpValues(op_amp.GetIdealBandwidth());
        constexpr int ground = -1;
        const int n_stage = AllocateInternalNode();
        MarkDynamic(n_stage);

        // G-U1 0 {stage} {in+} {in-} {Transconductance}
        AddDevice(std::make_unique<VccsDevice>(ground, n_stage, nodes[1], nodes[2], figures.Transconductance));
        // R-U1 {stage} 0 {Resistance}
        AddDevice(std::make_unique<ResistorDevice>(n_stage, ground, figures.Resistance));
        // C-U1 {stage} 0 {Capacitance}
        AddDevice(std::make_unique<CapacitorDevice>(n_stage, ground, figures.Capacitance, 0.0, false));
        // D-U1-P {stage} {V+} and D-U1-N {V-} {stage}
        AddDevice(std::make_unique<DiodeDevice>(n_stage, nodes[3], -1, figures.Clamp));
        AddDevice(std::make_unique<DiodeDevice>(nodes[4], n_stage, -1, figures.Clamp));
        // E-U1 {out} 0 {stage} 0 1
        Device *output = AddDevice(std::make_unique<VcvsDevice>(nodes[0], ground, n_stage, ground, AllocateBranch(), 1.0));
        m_Result.Currents.push_back({op_amp.GetName(), {{output, 0}}});
        return {};
    }

    /**
     * @brief   Builds the Boyle op-amp macromodel (e.g. uA741), element-for-element identical to
     *          `FormatOpAmpSubcircuit()` in `op_amp.cpp`.
     * @details
     * External terminals (`nodes`): `[0] = out (pin 6)`, `[1] = in+ (pin 3)`, `[2] = in- (pin 2)`,
     * `[3] = V+ (pin 7)`, `[4] = V- (pin 4)`.
     *
     * The macromodel has 5 functional blocks:
     * 1. **Differential Input Stage**: NPN pair `Q1`/`Q2` with collector loads `RC1`/`RC2`, phase-margin cap `C1`,
     *    emitter degeneration `RE1`/`RE2`, and tail current sink `IEE`/`REE`.
     * 2. **Intermediate Gain Stage & Miller Compensation**: common-mode `GCM` and differential `GA` driving
     *    gain resistor `R2` and Miller compensation capacitor `C2`.
     * 3. **Output Stage**: transconductance `GB`, load `RO2`, and $1\text{ }\Omega$ current-sense resistor `RSO`.
     * 4. **Short-Circuit Current Limiter**: sense amplifier `ECL` + diode clamp `D1`/`D2` + feedback `GCL`/`RCL`.
     * 5. **Positive & Negative Rail Swing Limiters**: reference sources `VC`/`VE` + diode strings `D3A`/`D3B` and
     *    `D4A`/`D4B` + feedback transconductances `GPL`/`GNL`.
     */
    std::expected<void, std::string> AddBoyleOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes) {
        const BoyleOpAmpValues v = ComputeBoyleOpAmpValues(op_amp.GetParameters());
        constexpr int ground = -1;
        const int out = nodes[0]; // SPICE pin 6
        const int inp = nodes[1]; // SPICE pin 3 (+)
        const int inm = nodes[2]; // SPICE pin 2 (-)
        const int vcc = nodes[3]; // SPICE pin 7 (V+)
        const int vee = nodes[4]; // SPICE pin 4 (V-)

        // Internal nodes named by physical role (with SPICE subcircuit node numbers noted alongside):
        const int n_col1 = AllocateInternalNode();      // SPICE node 80: Q1 collector
        const int n_col2 = AllocateInternalNode();      // SPICE node 90: Q2 collector
        const int n_emit1 = AllocateInternalNode();     // SPICE node 10: Q1 emitter
        const int n_emit2 = AllocateInternalNode();     // SPICE node 11: Q2 emitter
        const int n_tail = AllocateInternalNode();      // SPICE node 12: shared tail node
        const int n_gain = AllocateInternalNode();      // SPICE node 8:  high-impedance gain stage node
        const int n_out_int = AllocateInternalNode();   // SPICE node 1:  internal output before RSO
        const int n_cl_clamp = AllocateInternalNode();  // SPICE node 20: current-limit diode clamp node
        const int n_cl_sense = AllocateInternalNode();  // SPICE node 18: current-limit ECL output node
        const int n_pos_sense = AllocateInternalNode(); // SPICE node 70: positive swing limiter sense node
        const int n_pos_mid = AllocateInternalNode();   // SPICE node 131: midpoint between D3A and D3B
        const int n_pos_ref = AllocateInternalNode();   // SPICE node 13: positive swing reference (out + VC)
        const int n_neg_sense = AllocateInternalNode(); // SPICE node 60: negative swing limiter sense node
        const int n_neg_mid = AllocateInternalNode();   // SPICE node 141: midpoint between D4A and D4B
        const int n_neg_ref = AllocateInternalNode();   // SPICE node 14: negative swing reference (out - VE)

        // Miller compensation capacitor C2 makes the gain-stage node dynamic for LTE step control
        MarkDynamic(n_gain);

        BJTParameters pair;
        pair.SaturationCurrent = v.InputSaturationCurrent;
        pair.ForwardBeta = v.Beta;
        const DiodeParameters dm1{.SaturationCurrent = 1e-20};
        const DiodeParameters dm3{.SaturationCurrent = 1e-16};

        // --- 1. Differential Input Stage ---
        // RC1 7 80 / RC2 7 90
        AddDevice(std::make_unique<ResistorDevice>(vcc, n_col1, v.CollectorResistance));
        AddDevice(std::make_unique<ResistorDevice>(vcc, n_col2, v.CollectorResistance));
        // Q1 80 2 10 QM / Q2 90 3 11 QM
        AddDevice(std::make_unique<BjtDevice>(n_col1, inm, n_emit1, -1, -1, -1, pair, false));
        AddDevice(std::make_unique<BjtDevice>(n_col2, inp, n_emit2, -1, -1, -1, pair, false));
        // C1 80 90
        AddDevice(std::make_unique<CapacitorDevice>(n_col1, n_col2, v.CollectorCapacitance, 0.0, false));
        // RE1 10 12 / RE2 11 12
        AddDevice(std::make_unique<ResistorDevice>(n_emit1, n_tail, v.EmitterResistance));
        AddDevice(std::make_unique<ResistorDevice>(n_emit2, n_tail, v.EmitterResistance));
        // IEE 12 4 / REE 12 0
        AddDevice(std::make_unique<CurrentSourceDevice>(n_tail, vee, v.TailCurrent, ConstantValue));
        AddDevice(std::make_unique<ResistorDevice>(n_tail, ground, v.TailResistance));

        // --- 2. Intermediate Gain Stage & Miller Compensation ---
        // GCM 0 8 12 0 / GA 8 0 80 90
        AddDevice(std::make_unique<VccsDevice>(ground, n_gain, n_tail, ground, v.CommonModeTransconductance));
        AddDevice(std::make_unique<VccsDevice>(n_gain, ground, n_col1, n_col2, v.Transconductance));
        // R2 8 0 / C2 1 8
        AddDevice(std::make_unique<ResistorDevice>(n_gain, ground, v.GainStageResistance));
        AddDevice(std::make_unique<CapacitorDevice>(n_out_int, n_gain, v.CompensationCapacitance, 0.0, false));

        // --- 3. Output Stage ---
        // GB 1 0 8 0 / RO2 1 0 / RSO 1 6
        AddDevice(std::make_unique<VccsDevice>(n_out_int, ground, n_gain, ground, v.OutputGain));
        AddDevice(std::make_unique<ResistorDevice>(n_out_int, ground, v.GainStageLoad));
        Device *sense = AddDevice(std::make_unique<ResistorDevice>(n_out_int, out, v.OutputSenseResistance));

        // --- 4. Short-Circuit Current Limiter ---
        // ECL 18 0 1 6 / GCL 0 8 20 0 1 / RCL 20 0 10 / D1 18 20 DM1 / D2 20 18 DM1
        AddDevice(std::make_unique<VcvsDevice>(n_cl_sense, ground, n_out_int, out, AllocateBranch(), v.CurrentLimitGain));
        AddDevice(std::make_unique<VccsDevice>(ground, n_gain, n_cl_clamp, ground, 1.0));
        AddDevice(std::make_unique<ResistorDevice>(n_cl_clamp, ground, 10.0));
        AddDevice(std::make_unique<DiodeDevice>(n_cl_sense, n_cl_clamp, -1, dm1));
        AddDevice(std::make_unique<DiodeDevice>(n_cl_clamp, n_cl_sense, -1, dm1));

        // --- 5a. Positive Rail Swing Limiter ---
        // VC 13 6 / D3A 131 70 DM3 / D3B 13 131 DM3 / GPL 0 8 70 7 1 / RPLA 7 70 10 / RPLB 7 131 1k
        Device *vc =
            AddDevice(std::make_unique<VoltageSourceDevice>(n_pos_ref, out, AllocateBranch(), v.PositiveSwing, ConstantValue));
        AddDevice(std::make_unique<DiodeDevice>(n_pos_mid, n_pos_sense, -1, dm3));
        AddDevice(std::make_unique<DiodeDevice>(n_pos_ref, n_pos_mid, -1, dm3));
        AddDevice(std::make_unique<VccsDevice>(ground, n_gain, n_pos_sense, vcc, 1.0));
        AddDevice(std::make_unique<ResistorDevice>(vcc, n_pos_sense, 10.0));
        AddDevice(std::make_unique<ResistorDevice>(vcc, n_pos_mid, 1e3));

        // --- 5b. Negative Rail Swing Limiter ---
        // VE 6 14 / D4A 60 141 DM3 / D4B 141 14 DM3 / GNL 0 8 60 4 1 / RNLA 60 4 10 / RNLB 141 4 1k
        Device *ve =
            AddDevice(std::make_unique<VoltageSourceDevice>(out, n_neg_ref, AllocateBranch(), v.NegativeSwing, ConstantValue));
        AddDevice(std::make_unique<DiodeDevice>(n_neg_sense, n_neg_mid, -1, dm3));
        AddDevice(std::make_unique<DiodeDevice>(n_neg_mid, n_neg_ref, -1, dm3));
        AddDevice(std::make_unique<VccsDevice>(ground, n_gain, n_neg_sense, vee, 1.0));
        AddDevice(std::make_unique<ResistorDevice>(n_neg_sense, vee, 10.0));
        AddDevice(std::make_unique<ResistorDevice>(n_neg_mid, vee, 1e3));

        // --- Quiescent Supply Current ---
        // IP 7 4
        AddDevice(std::make_unique<CurrentSourceDevice>(vcc, vee, v.RestOfSupplyCurrent, ConstantValue));

        // Output pin current = sum of currents entering pin 6 through RSO, VC, and VE
        m_Result.Currents.push_back({op_amp.GetName(), {{sense, 1}, {vc, 1}, {ve, 0}}});
        return {};
    }

    std::expected<void, std::string> AddOpAmp(const OpAmp &op_amp, const std::vector<int> &nodes) {
        if (op_amp.IsIdeal()) {
            return AddIdealOpAmp(op_amp, nodes);
        }
        return AddBoyleOpAmp(op_amp, nodes);
    }

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
                                                const Parameters &parameters) {
        // Insert 0 V current-probe sources for any terminals followed by CCCS/CCVS elements in this circuit
        const auto probes = PlanProbes(circuit);
        if (!probes) {
            return std::unexpected(probes.error());
        }
        // entry_index -> terminal_index -> {probe_node_unknown, probe_branch_unknown}
        std::map<int, std::map<int, std::pair<int, int>>> probe_nodes;
        for (const ProbeSpec &probe : *probes) {
            const CircuitEntry &target = circuit.GetEntries()[static_cast<std::size_t>(probe.Entry)];
            const int original_unknown =
                node_map[static_cast<std::size_t>(target.Nodes[static_cast<std::size_t>(probe.Terminal)])];
            const int probe_node = AllocateInternalNode();
            const int probe_branch = AllocateBranch();
            AddDevice(std::make_unique<VoltageSourceDevice>(
                original_unknown, probe_node, probe_branch, 0.0,
                [](const double, const TimeContext &) { return 0.0; }));
            probe_nodes[probe.Entry][probe.Terminal] = {probe_node, probe_branch};
        }

        for (std::size_t index = 0; index < circuit.GetEntries().size(); ++index) {
            if (const auto result = AddEntry(circuit.GetEntries()[index], circuit, static_cast<int>(index), node_map,
                                             parameters, probe_nodes);
                !result) {
                return std::unexpected(result.error());
            }
        }
        return {};
    }
};

} // namespace

std::expected<CompiledCircuit, std::string> CompileCircuit(const Circuit &circuit) {
    return Builder(circuit).Build();
}

} // namespace Core::Native
