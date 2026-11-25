/**
 * @file    circuit.h
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#ifndef IMCSIM_CIRCUIT_H
#define IMCSIM_CIRCUIT_H

#include "circuit/components/component.h"
#include "circuit/library_model.h"
#include "circuit/solver_options.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

struct SubcircuitDefinition;

/**
 * @struct  CircuitEntry
 * @brief   A component of the circuit and the node of each of its terminals.
 * @details The component is not owned: whoever built the circuit must keep it alive while the circuit is used.
 */
struct CircuitEntry {
    const Component *Part = nullptr;
    /// One node per terminal, in terminal order; node 0 is ground.
    std::vector<int> Nodes;
};

/**
 * @struct  CircuitPort
 * @brief   A port of the S-parameter analysis: a node that is driven and measured through its reference impedance.
 */
struct CircuitPort {
    /// Index in the S matrix, from 1.
    int Number = 1;
    int Node = 0;
    double ReferenceOhms = 50.0;
};

/**
 * @struct  CircuitNodeVoltage
 * @brief   A voltage given to a node by a marker: where a transient starts it or where an operating point starts to
 * look for it.
 */
struct CircuitNodeVoltage {
    int Node = 0;
    double Volts = 0.0;
};

/**
 * @enum    NodeVoltageKind
 * @brief   What a marker does with the voltage it gives a node.
 */
enum class NodeVoltageKind {
    /// The node starts a transient at the voltage (SPICE `.ic`).
    INITIAL,
    /// The voltage is the guess the solver starts the operating point from (SPICE `.nodeset`).
    NODESET
};

/**
 * @class   Circuit
 * @brief   Topology of a circuit, without geometry: which nodes each component connects.
 */
class Circuit {
public:
    void Add(const Component &component, std::vector<int> nodes);
    void ReserveNode(int node);
    void AddSubcircuit(std::shared_ptr<SubcircuitDefinition> definition);
    void AddLibraryModel(LibraryModel model);
    void AddPort(const CircuitPort &port);
    void AddInitialVoltage(const CircuitNodeVoltage &initial_voltage);
    void AddNodeset(const CircuitNodeVoltage &nodeset);
    void NameNet(std::string_view net, int node);
    void SetSolverOptions(const SolverOptions &options);
    void AddDefinitionsOf(const Circuit &other);

    const std::vector<CircuitEntry> &GetEntries() const;
    const std::vector<std::shared_ptr<SubcircuitDefinition>> &GetSubcircuits() const;
    std::shared_ptr<const SubcircuitDefinition> FindSubcircuit(std::string_view name) const;
    const std::vector<LibraryModel> &GetLibraryModels() const;
    const std::vector<CircuitPort> &GetPorts() const;
    const std::vector<CircuitNodeVoltage> &GetInitialVoltages() const;
    const std::vector<CircuitNodeVoltage> &GetNodesets() const;
    const std::vector<CircuitNodeVoltage> &GetNodeVoltages(NodeVoltageKind kind) const;
    const SolverOptions &GetSolverOptions() const;
    const LibraryModel *FindLibraryModel(std::string_view name) const;
    std::optional<int> FindNodeOfNet(std::string_view net) const;
    const std::map<std::string, int, std::less<>> &GetNets() const;
    int GetNodeCount() const;
    bool HasGround() const;
    bool HasACSource() const;
    bool HasInitialConditions() const;
    bool HasFuse() const;
    bool HasThyristor() const;
    std::string ToSpiceNetlist(std::string_view analysis = "", bool add_current_probes = false) const;

private:
    std::vector<CircuitEntry> m_Entries;
    std::vector<std::shared_ptr<SubcircuitDefinition>> m_Subcircuits;
    std::vector<LibraryModel> m_LibraryModels;
    std::vector<CircuitPort> m_Ports;
    /// One entry per marker, so two markers on one node stay visible to the checks.
    std::vector<CircuitNodeVoltage> m_InitialVoltages;
    /// One entry per marker, for the same reason.
    std::vector<CircuitNodeVoltage> m_Nodesets;
    SolverOptions m_SolverOptions;
    /// Node of each net a label names, by the name in the form nets are compared in, so a behavioral source can read
    /// V(OUT).
    std::map<std::string, int, std::less<>> m_NodeOfNet;
    int m_NodeCount = 0;
};

// Parts
std::vector<const Component *> CollectParts(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_CIRCUIT_H
