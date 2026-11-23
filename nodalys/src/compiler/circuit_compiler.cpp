/**
 * @file    circuit_compiler.cpp
 * @brief   Lowers a CircuitDescription into a CompiledCircuit with allocated MNA unknowns and late branch bindings.
 */

#include "compiler/circuit_compiler.h"

#include "compiler/macromodels/electromechanical_macromodels.h"
#include "compiler/macromodels/logic_macromodels.h"
#include "compiler/macromodels/opamp_macromodels.h"
#include "devices/passive/fuse_device.h"
#include "devices/passive/passive_devices.h"
#include "devices/passive/smooth_logic.h"
#include "devices/passive/switch_device.h"
#include "devices/semiconductors/bjt_device.h"
#include "devices/semiconductors/diode_device.h"
#include "devices/semiconductors/mosfet_device.h"
#include "devices/sources/controlled_sources.h"
#include <algorithm>
#include <format>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

/// Control branch of a current-controlled source until Compile() binds it to its source, or refuses the circuit.
constexpr int UNBOUND_CONTROL_BRANCH = -1;

const VoltageSourceDevice *FindVoltageSource(const DevicesByName &sources, const std::string_view name) {
    const auto found = sources.find(name);
    if (found == sources.end() || found->second == nullptr) {
        return nullptr;
    }
    const IndependentSourceDevice *source = found->second->AsIndependentSource();
    return (source != nullptr && source->IsVoltageSource()) ? static_cast<const VoltageSourceDevice *>(source)
                                                            : nullptr;
}

DiodeParameters ScaleByArea(DiodeParameters parameters, const double area) {
    parameters.SaturationCurrent *= area;
    parameters.BreakdownCurrent *= area;
    parameters.JunctionCapacitance *= area;
    parameters.SeriesResistance /= area;
    return parameters;
}

BjtParameters ScaleByArea(BjtParameters parameters, const double area) {
    parameters.SaturationCurrent *= area;
    parameters.LeakageSaturationCurrent *= area;
    parameters.ForwardKneeCurrent *= area;
    parameters.EmitterCapacitance *= area;
    parameters.CollectorCapacitance *= area;
    parameters.BaseResistance /= area;
    parameters.CollectorResistance /= area;
    parameters.EmitterResistance /= area;
    return parameters;
}

} // namespace

std::expected<CompiledCircuit, std::string> CircuitCompiler::Compile(CircuitDescription description) {
    const int total_top_nodes = std::max(description.CircuitNodeCount, description.MaxNode + 1);
    const int circuit_node_count = std::max(total_top_nodes, 1);
    const int top_node_unknowns = std::max(0, circuit_node_count - 1);
    m_IsNodeUnknown.assign(static_cast<std::size_t>(top_node_unknowns), true);
    m_DynamicUnknown.assign(static_cast<std::size_t>(top_node_unknowns), false);

    m_Compiled.Thermal = description.Thermal;
    m_Compiled.Ports = std::move(description.Ports);
    m_Compiled.HasInitialConditions = description.HasInitialConditions;
    m_Errors = std::move(description.Errors);

    for (CircuitElementSpec &element : description.Elements) {
        std::visit([this](auto &spec) { Lower(spec); }, element);
    }

    MaterializeNodeDirectives();
    BindBehavioralCurrents();
    BindControlBranches();
    if (!m_Errors.empty()) {
        return std::unexpected(m_Errors.front());
    }

    m_Compiled.CircuitNodeCount = circuit_node_count;
    m_Compiled.CompiledNodeCount = static_cast<int>(std::ranges::count(m_IsNodeUnknown, true)) + 1;
    m_Compiled.System = std::make_unique<MnaSystem>(std::move(m_IsNodeUnknown));
    m_Compiled.DynamicNode = std::move(m_DynamicUnknown);
    for (const auto &device : m_Compiled.Devices) {
        device->SetTemperature(m_Compiled.Thermal);
    }
    return std::move(m_Compiled);
}

int CircuitCompiler::MapNode(const NodeId node) const {
    return node <= 0 ? -1 : node - 1;
}

int CircuitCompiler::AllocateInternalNode() {
    const int index = static_cast<int>(m_IsNodeUnknown.size());
    m_IsNodeUnknown.push_back(true);
    m_DynamicUnknown.push_back(false);
    return index;
}

int CircuitCompiler::AllocateBranch() {
    const int index = static_cast<int>(m_IsNodeUnknown.size());
    m_IsNodeUnknown.push_back(false);
    m_DynamicUnknown.push_back(false);
    return index;
}

void CircuitCompiler::MarkDynamic(const int unknown) {
    if (unknown >= 0 && static_cast<std::size_t>(unknown) < m_DynamicUnknown.size()) {
        m_DynamicUnknown[static_cast<std::size_t>(unknown)] = true;
    }
}

Device *CircuitCompiler::AddDevice(std::unique_ptr<Device> device) {
    Device *ptr = device.get();
    m_Compiled.Devices.push_back(std::move(device));
    return ptr;
}

void CircuitCompiler::RegisterElement(const std::string &name, Device *device) {
    m_Compiled.ElementsByName[name] = device;
    m_Compiled.Currents.push_back({name, {{device, 0}}});
}

void CircuitCompiler::RegisterElementAlias(const std::string &name, Device *device) {
    m_Compiled.ElementsByName[name] = device;
}

void CircuitCompiler::RegisterTerminalCurrents(const std::string &name,
                                               const Device *device,
                                               const std::initializer_list<std::string_view> terminals) {
    int terminal_index = 0;
    for (const std::string_view terminal : terminals) {
        m_Compiled.Currents.push_back({std::format("{}.{}", name, terminal), {{device, terminal_index++}}});
    }
}

void CircuitCompiler::AddCurrentReadout(std::string name, std::vector<CurrentContribution> contributions) {
    m_Compiled.Currents.push_back({std::move(name), std::move(contributions)});
}

void CircuitCompiler::AddNoiseGroup(std::string name, std::vector<const Device *> devices) {
    m_Compiled.NoiseGroups.push_back({std::move(name), std::move(devices)});
}

void CircuitCompiler::AddNodeSeed(const int node, const double voltage) {
    m_Compiled.NodeSeeds.push_back({.Node = node, .Voltage = voltage});
}

std::size_t CircuitCompiler::DeviceCount() const {
    return m_Compiled.Devices.size();
}

std::vector<const Device *> CircuitCompiler::DevicesFrom(const std::size_t first_index) const {
    std::vector<const Device *> devices;
    for (std::size_t i = first_index; i < m_Compiled.Devices.size(); ++i) {
        devices.push_back(m_Compiled.Devices[i].get());
    }
    return devices;
}

void CircuitCompiler::AddRawLogicGate(std::string name,
                                      const LogicGateType type,
                                      const int u_out,
                                      std::vector<int> u_inputs,
                                      const SmoothLogicParameters &params) {
    int n_int = -1;
    if (params.PropagationDelay > 0.0) {
        n_int = AllocateInternalNode();
        MarkDynamic(n_int);
    }
    Device *gate = AddDevice(std::make_unique<SmoothLogicDevice>(type, u_out, std::move(u_inputs), n_int, params));
    m_Compiled.Currents.push_back({name, {{gate, 0}}});
    m_Compiled.NoiseGroups.push_back({std::move(name), {gate}});
}

void CircuitCompiler::Lower(BehavioralSpec &spec) {
    const int u_pos = MapNode(spec.Pos);
    const int u_neg = MapNode(spec.Neg);
    BehavioralSourceDevice *device = nullptr;
    if (spec.IsVoltage) {
        const int branch = AllocateBranch();
        device = static_cast<BehavioralSourceDevice *>(
            AddDevice(std::make_unique<BehavioralVoltageDevice>(spec.Name,
                                                                u_pos,
                                                                u_neg,
                                                                branch,
                                                                std::move(spec.Expr),
                                                                std::move(spec.Bindings))));
    } else {
        device = static_cast<BehavioralSourceDevice *>(
            AddDevice(std::make_unique<BehavioralCurrentDevice>(spec.Name,
                                                                u_pos,
                                                                u_neg,
                                                                std::move(spec.Expr),
                                                                std::move(spec.Bindings))));
    }
    RegisterElement(spec.Name, device);
    for (BehavioralCurrentInput &current : spec.CurrentInputs) {
        m_PendingCurrents.push_back({.Owner = std::move(current.Owner),
                                     .Device = device,
                                     .Input = current.Input,
                                     .Source = std::move(current.Source)});
    }
}

void CircuitCompiler::Lower(const BjtSpec &spec) {
    const BjtParameters params = ScaleByArea(spec.Params, spec.Area);
    const int uc = MapNode(spec.Collector);
    const int ub = MapNode(spec.Base);
    const int ue = MapNode(spec.Emitter);
    const int int_c = (params.CollectorResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_b = (params.BaseResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_e = (params.EmitterResistance > 0.0) ? AllocateInternalNode() : -1;
    if (params.TransitTime > 0.0 || params.EmitterCapacitance > 0.0 || params.CollectorCapacitance > 0.0) {
        MarkDynamic(int_c >= 0 ? int_c : uc);
        MarkDynamic(int_b >= 0 ? int_b : ub);
        MarkDynamic(int_e >= 0 ? int_e : ue);
    }
    Device *dev = AddDevice(std::make_unique<BjtDevice>(uc, ub, ue, int_c, int_b, int_e, params, !spec.IsNpn));
    m_Compiled.ElementsByName[spec.Name] = dev;
    RegisterTerminalCurrents(spec.Name, dev, {"C", "B", "E"});
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const BoyleOpAmpSpec &spec) {
    CompileBoyleOpAmp(*this, spec);
}

void CircuitCompiler::Lower(const CapacitorSpec &spec) {
    const int u1 = MapNode(spec.N1);
    const int u2 = MapNode(spec.N2);
    MarkDynamic(u1);
    MarkDynamic(u2);
    const bool has_ic = (spec.InitialVoltage != 0.0);
    if (has_ic) {
        m_Compiled.HasInitialConditions = true;
    }
    auto capacitor = std::make_unique<CapacitorDevice>(u1, u2, spec.Capacitance, spec.InitialVoltage, has_ic);
    m_Capacitors.push_back(capacitor.get());
    Device *dev = AddDevice(std::move(capacitor));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const CccsSpec &spec) {
    auto device = std::make_unique<CccsDevice>(MapNode(spec.Pos), MapNode(spec.Neg), UNBOUND_CONTROL_BRANCH, spec.Gain);
    CccsDevice *cccs = device.get();
    RegisterElement(spec.Name, AddDevice(std::move(device)));
    m_PendingControls.push_back({.Owner = spec.Name, .Source = spec.ControlSource, .Bind = [cccs](const int ctrl) {
                                     cccs->BindControlBranch(ctrl);
                                 }});
}

void CircuitCompiler::Lower(const CcvsSpec &spec) {
    const int branch = AllocateBranch();
    auto device = std::make_unique<CcvsDevice>(MapNode(spec.Pos),
                                               MapNode(spec.Neg),
                                               branch,
                                               UNBOUND_CONTROL_BRANCH,
                                               spec.Transresistance);
    CcvsDevice *ccvs = device.get();
    RegisterElement(spec.Name, AddDevice(std::move(device)));
    m_PendingControls.push_back({.Owner = spec.Name, .Source = spec.ControlSource, .Bind = [ccvs](const int ctrl) {
                                     ccvs->BindControlBranch(ctrl);
                                 }});
}

void CircuitCompiler::Lower(const DFlipFlopSpec &spec) {
    CompileDFlipFlop(*this, spec);
}

void CircuitCompiler::Lower(const DiodeSpec &spec) {
    const DiodeParameters params = ScaleByArea(spec.Params, spec.Area);
    const int ua = MapNode(spec.Anode);
    const int uc = MapNode(spec.Cathode);
    const int internal = (params.SeriesResistance > 0.0) ? AllocateInternalNode() : -1;
    if (params.JunctionCapacitance > 0.0 || params.TransitTime > 0.0) {
        MarkDynamic(ua);
        MarkDynamic(internal >= 0 ? internal : uc);
    }
    Device *dev = AddDevice(std::make_unique<DiodeDevice>(ua, uc, internal, params));
    RegisterElement(spec.Name, dev);
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const FuseSpec &spec) {
    Device *dev = AddDevice(std::make_unique<FuseDevice>(MapNode(spec.NodeA), MapNode(spec.NodeB), spec.Params));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const IdealOpAmpSpec &spec) {
    CompileIdealOpAmp(*this, spec);
}

void CircuitCompiler::Lower(IndependentSourceSpec &spec) {
    const int u_pos = MapNode(spec.Pos);
    const int u_neg = MapNode(spec.Neg);
    if (spec.IsVoltage) {
        const int branch = AllocateBranch();
        auto source = std::make_unique<VoltageSourceDevice>(u_pos,
                                                            u_neg,
                                                            branch,
                                                            spec.Spec.Dc,
                                                            std::move(spec.Spec.Waveform),
                                                            spec.Spec.AcMagnitude,
                                                            spec.Spec.Periodicity);
        InstallSource(spec.Name, std::move(source), std::move(spec.Spec));
        return;
    }
    auto source = std::make_unique<CurrentSourceDevice>(u_pos,
                                                        u_neg,
                                                        spec.Spec.Dc,
                                                        std::move(spec.Spec.Waveform),
                                                        spec.Spec.AcMagnitude,
                                                        spec.Spec.Periodicity);
    InstallSource(spec.Name, std::move(source), std::move(spec.Spec));
}

void CircuitCompiler::Lower(const InductorSpec &spec) {
    const int branch = AllocateBranch();
    const bool has_ic = (spec.InitialCurrent != 0.0);
    if (has_ic) {
        m_Compiled.HasInitialConditions = true;
    }
    Device *dev = AddDevice(std::make_unique<InductorDevice>(MapNode(spec.N1),
                                                             MapNode(spec.N2),
                                                             branch,
                                                             spec.Inductance,
                                                             spec.InitialCurrent,
                                                             has_ic));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const InitialVoltageSpec &spec) {
    const int mapped = MapNode(spec.Node);
    if (mapped >= 0) {
        m_InitialNodeVoltages[mapped] = spec.Voltage;
    }
}

void CircuitCompiler::Lower(const LogicGateSpec &spec) {
    std::vector<int> u_inputs;
    u_inputs.reserve(spec.Inputs.size());
    for (const NodeId in_id : spec.Inputs) {
        u_inputs.push_back(MapNode(in_id));
    }
    AddRawLogicGate(spec.Name, spec.Type, MapNode(spec.Out), std::move(u_inputs), spec.Params);
}

void CircuitCompiler::Lower(const MosfetSpec &spec) {
    const int ud = MapNode(spec.Drain);
    const int ug = MapNode(spec.Gate);
    const int us = MapNode(spec.Source);
    const int int_d = (spec.Params.DrainResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_s = (spec.Params.SourceResistance > 0.0) ? AllocateInternalNode() : -1;
    if (spec.Params.GateSourceOverlap > 0.0 || spec.Params.GateDrainOverlap > 0.0) {
        MarkDynamic(int_d >= 0 ? int_d : ud);
        MarkDynamic(ug);
        MarkDynamic(int_s >= 0 ? int_s : us);
    }
    Device *dev = AddDevice(std::make_unique<MosfetDevice>(ud, ug, us, int_d, int_s, spec.Params, !spec.IsNmos));
    m_Compiled.ElementsByName[spec.Name] = dev;
    RegisterTerminalCurrents(spec.Name, dev, {"D", "G", "S"});
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const NodesetSpec &spec) {
    const int mapped = MapNode(spec.Node);
    if (mapped >= 0) {
        m_NodesetVoltages[mapped] = spec.Voltage;
    }
}

void CircuitCompiler::Lower(const PotentiometerSpec &spec) {
    CompilePotentiometer(*this, spec);
}

void CircuitCompiler::Lower(const RelaySpec &spec) {
    CompileRelay(*this, spec);
}

void CircuitCompiler::Lower(const ResistorSpec &spec) {
    Device *dev = AddDevice(std::make_unique<ResistorDevice>(MapNode(spec.N1), MapNode(spec.N2), spec.Resistance));
    RegisterElement(spec.Name, dev);
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const SwitchSpec &spec) {
    Device *dev = AddDevice(std::make_unique<SwitchDevice>(MapNode(spec.NodeA),
                                                           MapNode(spec.NodeB),
                                                           MapNode(spec.CtrlPos),
                                                           MapNode(spec.CtrlNeg),
                                                           spec.Params));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const TransformerSpec &spec) {
    CompileTransformer(*this, spec);
}

void CircuitCompiler::Lower(const VccsSpec &spec) {
    Device *dev = AddDevice(std::make_unique<VccsDevice>(MapNode(spec.Pos),
                                                         MapNode(spec.Neg),
                                                         MapNode(spec.CtrlPos),
                                                         MapNode(spec.CtrlNeg),
                                                         spec.Transconductance));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const VcvsSpec &spec) {
    const int branch = AllocateBranch();
    Device *dev = AddDevice(std::make_unique<VcvsDevice>(MapNode(spec.Pos),
                                                         MapNode(spec.Neg),
                                                         MapNode(spec.CtrlPos),
                                                         MapNode(spec.CtrlNeg),
                                                         branch,
                                                         spec.Gain));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::InstallSource(const std::string &name,
                                    std::unique_ptr<IndependentSourceDevice> source,
                                    SourceSpec spec) {
    source->SetBreakpoints(std::move(spec.Breakpoints));
    source->SetSeedCallback(std::move(spec.SetSeed));
    source->SetCallbackFactory(std::move(spec.CallbackFactory));
    Device *device = AddDevice(std::move(source));
    // SPICE names ignore case, so "V1" and "v1" are the same source and the second one is a duplicate
    if (!m_Compiled.SourcesByName.try_emplace(name, device).second) {
        m_Errors.push_back(std::format("{}: another source of the circuit has the same name", name));
    }
    RegisterElement(name, device);
}

void CircuitCompiler::MaterializeNodeDirectives() {
    for (const auto &[node, voltage] : m_InitialNodeVoltages) {
        m_Compiled.InitialConditions.push_back({.Node = node, .Voltage = voltage});
    }
    if (!m_InitialNodeVoltages.empty()) {
        for (CapacitorDevice *capacitor : m_Capacitors) {
            capacitor->TakeInitialVoltageFrom(m_InitialNodeVoltages);
        }
    }
    m_Capacitors.clear();
    m_InitialNodeVoltages.clear();
    for (const auto &[node, voltage] : m_NodesetVoltages) {
        m_Compiled.Nodesets.push_back({.Node = node, .Voltage = voltage});
    }
    m_NodesetVoltages.clear();
}

void CircuitCompiler::BindBehavioralCurrents() {
    for (const PendingCurrent &current : m_PendingCurrents) {
        const VoltageSourceDevice *source = FindVoltageSource(m_Compiled.SourcesByName, current.Source);
        if (source == nullptr) {
            m_Errors.push_back(
                std::format("{}: i({}) must name a voltage source of the circuit", current.Owner, current.Source));
            continue;
        }
        current.Device->BindCurrent(current.Input, source->GetBranch());
    }
    m_PendingCurrents.clear();
}

void CircuitCompiler::BindControlBranches() {
    for (const PendingControl &control : m_PendingControls) {
        const VoltageSourceDevice *source = FindVoltageSource(m_Compiled.SourcesByName, control.Source);
        if (source == nullptr) {
            m_Errors.push_back(std::format("{}: its control source {} is not a voltage source of the circuit",
                                           control.Owner,
                                           control.Source));
            continue;
        }
        control.Bind(source->GetBranch());
    }
    m_PendingControls.clear();
}

} // namespace Nodalys
