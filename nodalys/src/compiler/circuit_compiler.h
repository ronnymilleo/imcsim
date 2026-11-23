/**
 * @file    circuit_compiler.h
 * @brief   Compiles a collected CircuitDescription into an MNA CompiledCircuit with allocated unknowns and bound
 *          control branches.
 */

#ifndef NODALYS_COMPILER_CIRCUIT_COMPILER_H
#define NODALYS_COMPILER_CIRCUIT_COMPILER_H

#include "compiler/circuit_description.h"
#include "compiler/compiled_circuit.h"
#include "devices/device.h"
#include "devices/sources/independent_sources.h"
#include "nodalys/types.h"
#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

class CapacitorDevice;

/**
 * @class   CircuitCompiler
 * @brief   Lowers a passive CircuitDescription into a CompiledCircuit: allocates MNA unknowns, lowers primitives and
 *          macromodels, materializes node directives, and binds late branch references.
 */
class CircuitCompiler {
public:
    /// Compiles a collected circuit description into an executable MNA CompiledCircuit.
    [[nodiscard]] std::expected<CompiledCircuit, std::string> Compile(CircuitDescription description);

    // MNA unknown allocation and registration primitives (shared with macromodel modules)
    [[nodiscard]] int MapNode(NodeId node) const;
    int AllocateInternalNode();
    int AllocateBranch();
    void MarkDynamic(int unknown);
    Device *AddDevice(std::unique_ptr<Device> device);
    void RegisterElement(const std::string &name, Device *device);
    void RegisterElementAlias(const std::string &name, Device *device);
    void RegisterTerminalCurrents(const std::string &name,
                                  const Device *device,
                                  std::initializer_list<std::string_view> terminals);
    void AddCurrentReadout(std::string name, std::vector<CurrentContribution> contributions);
    void AddNoiseGroup(std::string name, std::vector<const Device *> devices);
    void AddNodeSeed(int node, double voltage);
    [[nodiscard]] std::size_t DeviceCount() const;
    [[nodiscard]] std::vector<const Device *> DevicesFrom(std::size_t first_index) const;
    void AddRawLogicGate(std::string name,
                         LogicGateType type,
                         int u_out,
                         std::vector<int> u_inputs,
                         const SmoothLogicParameters &params);

private:
    /// Current read by a behavioral source, to be bound once every source exists.
    struct PendingCurrent {
        std::string Owner;
        BehavioralSourceDevice *Device = nullptr;
        std::size_t Input = 0;
        std::string Source;
    };

    /// Controlling source of a CCCS/CCVS, to be bound once every source exists.
    struct PendingControl {
        std::string Owner;
        std::string Source;
        std::function<void(int)> Bind;
    };

    std::vector<PendingCurrent> m_PendingCurrents;
    std::vector<PendingControl> m_PendingControls;
    std::vector<CapacitorDevice *> m_Capacitors;
    std::vector<std::string> m_Errors;
    std::vector<bool> m_IsNodeUnknown;
    std::vector<bool> m_DynamicUnknown;
    std::map<int, double> m_InitialNodeVoltages;
    std::map<int, double> m_NodesetVoltages;
    CompiledCircuit m_Compiled;

    // Element lowering (overloaded for std::visit dispatch)
    void Lower(BehavioralSpec &spec);
    void Lower(const BjtSpec &spec);
    void Lower(const BoyleOpAmpSpec &spec);
    void Lower(const CapacitorSpec &spec);
    void Lower(const CccsSpec &spec);
    void Lower(const CcvsSpec &spec);
    void Lower(const DFlipFlopSpec &spec);
    void Lower(const DiodeSpec &spec);
    void Lower(const FuseSpec &spec);
    void Lower(const IdealOpAmpSpec &spec);
    void Lower(IndependentSourceSpec &spec);
    void Lower(const InductorSpec &spec);
    void Lower(const InitialVoltageSpec &spec);
    void Lower(const LogicGateSpec &spec);
    void Lower(const MosfetSpec &spec);
    void Lower(const NodesetSpec &spec);
    void Lower(const PotentiometerSpec &spec);
    void Lower(const RelaySpec &spec);
    void Lower(const ResistorSpec &spec);
    void Lower(const SwitchSpec &spec);
    void Lower(const TransformerSpec &spec);
    void Lower(const VccsSpec &spec);
    void Lower(const VcvsSpec &spec);

    // Finalization helpers
    void InstallSource(const std::string &name, std::unique_ptr<IndependentSourceDevice> source, SourceSpec spec);
    void MaterializeNodeDirectives();
    void BindBehavioralCurrents();
    void BindControlBranches();
};

} // namespace Nodalys

#endif // NODALYS_COMPILER_CIRCUIT_COMPILER_H
