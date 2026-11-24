/**
 * @file    circuit_compiler.cpp
 * @brief   Lowers a CircuitDescription into a CompiledCircuit with allocated MNA unknowns and late branch bindings.
 */

#include "compiler/circuit_compiler.h"

#include "devices/passive/passive_devices.h"
#include "devices/passive/smooth_logic.h"
#include <algorithm>
#include <format>
#include <utility>
#include <variant>

namespace Nodalys {

namespace {

const VoltageSourceDevice *FindVoltageSource(const DevicesByName &sources, const std::string_view name) {
    const auto found = sources.find(name);
    if (found == sources.end() || found->second == nullptr) {
        return nullptr;
    }
    const IndependentSourceDevice *source = found->second->AsIndependentSource();
    return (source != nullptr && source->IsVoltageSource()) ? static_cast<const VoltageSourceDevice *>(source)
                                                            : nullptr;
}

} // namespace

/**
 * @brief   Compiles a collected CircuitDescription into an executable MNA CompiledCircuit.
 * @param[in] description  Collected element specifications, ports, and thermal conditions.
 * @return  Compiled circuit ready for simulation, or a diagnostic error message.
 */
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

/**
 * @brief   Maps a 1-based circuit NodeId to its 0-based MNA unknown index.
 * @param[in] node  Circuit node identifier (`0` for ground).
 * @return  Zero-based MNA unknown index, or `-1` for ground.
 */
int CircuitCompiler::MapNode(const NodeId node) const {
    return node <= 0 ? -1 : node - 1;
}

/**
 * @brief   Allocates a new internal node-voltage unknown in the MNA system.
 * @return  Zero-based unknown index of the newly allocated internal node.
 */
int CircuitCompiler::AllocateInternalNode() {
    const int index = static_cast<int>(m_IsNodeUnknown.size());
    m_IsNodeUnknown.push_back(true);
    m_DynamicUnknown.push_back(false);
    return index;
}

/**
 * @brief   Allocates a new branch-current unknown in the MNA system.
 * @return  Zero-based unknown index of the newly allocated branch current.
 */
int CircuitCompiler::AllocateBranch() {
    const int index = static_cast<int>(m_IsNodeUnknown.size());
    m_IsNodeUnknown.push_back(false);
    m_DynamicUnknown.push_back(false);
    return index;
}

/**
 * @brief   Marks an unknown as connected to a dynamic energy-storage element for transient LTE monitoring.
 * @param[in] unknown  Zero-based MNA unknown index (negative indices are ignored).
 */
void CircuitCompiler::MarkDynamic(const int unknown) {
    if (unknown >= 0 && static_cast<std::size_t>(unknown) < m_DynamicUnknown.size()) {
        m_DynamicUnknown[static_cast<std::size_t>(unknown)] = true;
    }
}

/**
 * @brief   Takes ownership of a device and appends it to the compiled circuit.
 * @param[in] device  Device instance to register.
 * @return  Non-owning pointer to the stored device.
 */
Device *CircuitCompiler::AddDevice(std::unique_ptr<Device> device) {
    Device *ptr = device.get();
    m_Compiled.Devices.push_back(std::move(device));
    return ptr;
}

/**
 * @brief   Registers a named element for lookup and records a single-terminal current readout for terminal 0.
 * @param[in] name    Element designator.
 * @param[in] device  Non-owning pointer to the compiled device.
 */
void CircuitCompiler::RegisterElement(const std::string &name, Device *device) {
    m_Compiled.ElementsByName[name] = device;
    m_Compiled.Currents.push_back({name, {{device, 0}}});
}

/**
 * @brief   Registers an element name in the lookup table without creating a default current readout.
 * @param[in] name    Element designator or alias.
 * @param[in] device  Non-owning pointer to the compiled device.
 */
void CircuitCompiler::RegisterElementAlias(const std::string &name, Device *device) {
    m_Compiled.ElementsByName[name] = device;
}

/**
 * @brief   Registers named multi-terminal current readouts (`<name>.<terminal>`) for a device.
 * @param[in] name       Element designator prefix.
 * @param[in] device     Non-owning pointer to the compiled device.
 * @param[in] terminals  Ordered terminal suffixes matching terminal indices `0, 1, ...`.
 */
void CircuitCompiler::RegisterTerminalCurrents(const std::string &name,
                                               Device *device,
                                               const std::initializer_list<std::string_view> terminals) {
    int terminal_index = 0;
    for (const std::string_view terminal : terminals) {
        m_Compiled.Currents.push_back({std::format("{}.{}", name, terminal), {{device, terminal_index++}}});
    }
}

/**
 * @brief   Appends a composite current readout summing contributions from one or more device terminals.
 * @param[in] name           Readout label exposed in analysis results.
 * @param[in] contributions  List of `(device, terminal)` contributions to sum.
 */
void CircuitCompiler::AddCurrentReadout(std::string name, std::vector<CurrentContribution> contributions) {
    m_Compiled.Currents.push_back({std::move(name), std::move(contributions)});
}

/**
 * @brief   Groups one or more internal devices under a single element name for `.noise` reporting.
 * @param[in] name     Element designator shown in noise breakdown results.
 * @param[in] devices  Non-owning pointers to the contributing devices.
 */
void CircuitCompiler::AddNoiseGroup(std::string name, std::vector<const Device *> devices) {
    m_Compiled.NoiseGroups.push_back({std::move(name), std::move(devices)});
}

/**
 * @brief   Records an internal DC operating-point initial guess for a node unknown.
 * @param[in] node     Zero-based MNA node unknown index.
 * @param[in] voltage  Seed voltage in Volts.
 */
void CircuitCompiler::AddNodeSeed(const int node, const double voltage) {
    m_Compiled.NodeSeeds.push_back({.Node = node, .Voltage = voltage});
}

/**
 * @brief   Returns the number of devices added to the compiled circuit so far.
 * @return  Current device count.
 */
std::size_t CircuitCompiler::DeviceCount() const {
    return m_Compiled.Devices.size();
}

/**
 * @brief   Collects non-owning pointers to all devices added at or after `first_index`.
 * @param[in] first_index  Starting index in the compiled device list.
 * @return  Vector of device pointers from `first_index` to the end.
 */
std::vector<const Device *> CircuitCompiler::DevicesFrom(const std::size_t first_index) const {
    std::vector<const Device *> devices;
    for (std::size_t i = first_index; i < m_Compiled.Devices.size(); ++i) {
        devices.push_back(m_Compiled.Devices[i].get());
    }
    return devices;
}

/**
 * @brief   Compiles a smooth logic gate directly on MNA unknown indices (used by macromodels like flip-flops).
 * @param[in] name      Element designator.
 * @param[in] type      Logic gate function.
 * @param[in] u_out     Output MNA unknown index.
 * @param[in] u_inputs  Input MNA unknown indices.
 * @param[in] params    Smooth logic gate parameters.
 */
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
