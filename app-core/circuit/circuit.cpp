/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 * @details The SPICE netlist of a circuit is written in circuit_netlist.cpp and the current probes are in
 *          circuit_currents.cpp; the three define what circuit.h declares.
 */

#include "circuit/circuit.h"

#include "circuit/circuit_netlist.h"
#include "circuit/components/net_label.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace Core {

namespace {

bool IsACSource(const Component &component) {
    return IsSource(component.GetType())
        && static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

} // namespace

/**
 * @brief   Adds a component and the nodes its terminals connect to.
 * @param[in] component  Component to add; it must outlive the circuit.
 * @param[in] nodes      Node of each terminal, in terminal order; node 0 is ground.
 */
void Circuit::Add(const Component &component, std::vector<int> nodes) {
    for (const int node : nodes) {
        m_NodeCount = std::max(m_NodeCount, node + 1);
    }
    m_Entries.push_back({&component, std::move(nodes)});
}

/**
 * @brief   Counts a node that no part may reach, such as the pin of a block that nothing inside uses, so the extra
 *          nodes of the netlist are numbered after it.
 * @param[in] node  Node number; GetNodeCount() grows to cover it.
 */
void Circuit::ReserveNode(const int node) {
    m_NodeCount = std::max(m_NodeCount, node + 1);
}

/**
 * @brief   Adds a subcircuit definition, which the instances of the circuit refer to by name.
 * @param[in] definition  Definition to write as a .subckt block; it is shared, not copied.
 */
void Circuit::AddSubcircuit(std::shared_ptr<SubcircuitDefinition> definition) {
    m_Subcircuits.push_back(std::move(definition));
}

/**
 * @brief   Adds a model from a library, which the diodes and transistors of the circuit can use by name.
 * @param[in] model  Model to write in the netlist, under GetLibraryModelSpiceName().
 */
void Circuit::AddLibraryModel(LibraryModel model) {
    m_LibraryModels.push_back(std::move(model));
}

/**
 * @brief   Adds a port of the S-parameter analysis.
 * @param[in] port  Port to add; its node counts toward GetNodeCount().
 * @note    The netlist leaves ports out; the S-parameter run writes them itself.
 */
void Circuit::AddPort(const CircuitPort &port) {
    m_NodeCount = std::max(m_NodeCount, port.Node + 1);
    m_Ports.push_back(port);
}

/**
 * @brief   Adds the voltage a node starts a transient at.
 * @param[in] initial_voltage  Node and voltage; the node counts toward GetNodeCount().
 * @note    Only a transient reads it. The netlist writes it as ".ic"; CheckCircuit() refuses two different voltages on
 *          one node, ground, and a node no part is connected to. In a block body it sets the node of each instance;
 *          see ResolveNodeVoltages().
 */
void Circuit::AddInitialVoltage(const CircuitNodeVoltage &initial_voltage) {
    m_NodeCount = std::max(m_NodeCount, initial_voltage.Node + 1);
    m_InitialVoltages.push_back(initial_voltage);
}

/**
 * @brief   Adds the voltage the solver starts a node at when it looks for the operating point.
 * @param[in] nodeset  Node and voltage; the node counts toward GetNodeCount().
 * @note    Every analysis that starts from an operating point reads it, and the solution is free to end elsewhere,
 *          which is how it picks one of the stable states of a circuit that has several. The netlist writes it as
 *          ".nodeset"; CheckCircuit() refuses what it refuses for an initial voltage. In a block body it sets the
 *          node of each instance; see ResolveNodeVoltages().
 */
void Circuit::AddNodeset(const CircuitNodeVoltage &nodeset) {
    m_NodeCount = std::max(m_NodeCount, nodeset.Node + 1);
    m_Nodesets.push_back(nodeset);
}

/**
 * @brief   Gives a node the name a net label gave it, so expressions can name it.
 * @param[in] net   Name of the net, compared without regard to case.
 * @param[in] node  Its node.
 */
void Circuit::NameNet(const std::string_view net, const int node) {
    m_NodeOfNet[NetLabel::NormalizeNet(net)] = node;
}

/**
 * @brief   Sets the options of the solver, which the netlist writes and the backends read.
 * @param[in] options  Tolerances, integration method and nominal temperature.
 */
void Circuit::SetSolverOptions(const SolverOptions &options) {
    m_SolverOptions = options;
}

/**
 * @brief   Adds the subcircuit definitions, library models, net names, initial voltages, nodesets and solver options of
 *          another circuit, as a circuit derived from it needs.
 * @param[in] other  Circuit whose definitions are shared and whose models, net names, initial voltages, nodesets and
 *                   solver options are copied.
 */
void Circuit::AddDefinitionsOf(const Circuit &other) {
    for (const auto &definition : other.m_Subcircuits) {
        m_Subcircuits.push_back(definition);
    }
    for (const LibraryModel &model : other.m_LibraryModels) {
        m_LibraryModels.push_back(model);
    }
    for (const auto &[net, node] : other.m_NodeOfNet) {
        m_NodeOfNet[net] = node;
    }
    for (const CircuitNodeVoltage &initial_voltage : other.m_InitialVoltages) {
        AddInitialVoltage(initial_voltage);
    }
    for (const CircuitNodeVoltage &nodeset : other.m_Nodesets) {
        AddNodeset(nodeset);
    }
    m_SolverOptions = other.m_SolverOptions;
}

/**
 * @brief   Returns the components of the circuit, in the order they were added.
 * @return  Every component with the nodes of its terminals.
 */
const std::vector<CircuitEntry> &Circuit::GetEntries() const {
    return m_Entries;
}

/**
 * @brief   Returns the subcircuit definitions, in the order they were added.
 * @return  Every definition, used by an instance or not.
 */
const std::vector<std::shared_ptr<SubcircuitDefinition>> &Circuit::GetSubcircuits() const {
    return m_Subcircuits;
}

/**
 * @brief   Finds a subcircuit definition by name.
 * @param[in] name  Name of the definition.
 * @return  The definition, or null when the circuit has none of that name.
 */
std::shared_ptr<const SubcircuitDefinition> Circuit::FindSubcircuit(const std::string_view name) const {
    const auto found = std::ranges::find(m_Subcircuits, name, &SubcircuitDefinition::Name);
    return found != m_Subcircuits.end() ? *found : nullptr;
}

/**
 * @brief   Returns the library models of the circuit.
 * @return  Every model, used by a part or not.
 */
const std::vector<LibraryModel> &Circuit::GetLibraryModels() const {
    return m_LibraryModels;
}

/**
 * @brief   Returns the ports of the S-parameter analysis.
 * @return  The ports in the order they were added, which is not their number.
 */
const std::vector<CircuitPort> &Circuit::GetPorts() const {
    return m_Ports;
}

/**
 * @brief   Returns the initial voltages of the nodes.
 * @return  One entry per marker, in the order they were added.
 */
const std::vector<CircuitNodeVoltage> &Circuit::GetInitialVoltages() const {
    return m_InitialVoltages;
}

/**
 * @brief   Returns the nodesets of the nodes.
 * @return  One entry per marker, in the order they were added.
 */
const std::vector<CircuitNodeVoltage> &Circuit::GetNodesets() const {
    return m_Nodesets;
}

/**
 * @brief   Returns the markers of one kind.
 * @param[in] kind  Initial voltages or nodesets.
 * @return  One entry per marker, in the order they were added.
 */
const std::vector<CircuitNodeVoltage> &Circuit::GetNodeVoltages(const NodeVoltageKind kind) const {
    return kind == NodeVoltageKind::INITIAL ? m_InitialVoltages : m_Nodesets;
}

/**
 * @brief   Returns the options of the solver.
 * @return  The defaults unless SetSolverOptions() changed them.
 */
const SolverOptions &Circuit::GetSolverOptions() const {
    return m_SolverOptions;
}

/**
 * @brief   Finds a library model by the name its library gives it.
 * @param[in] name  Name of the model.
 * @return  The model, or null when the circuit has none of that name.
 */
const LibraryModel *Circuit::FindLibraryModel(const std::string_view name) const {
    const auto found = std::ranges::find(m_LibraryModels, name, &LibraryModel::Name);
    return found != m_LibraryModels.end() ? &*found : nullptr;
}

/**
 * @brief   Finds the node of a net by its name.
 * @param[in] net  Name as written, in any case.
 * @return  The node a label named so is on; 0 for "0" and "GND", which always mean ground; no value for a name no
 *          label gives.
 */
std::optional<int> Circuit::FindNodeOfNet(const std::string_view net) const {
    const std::string compared = NetLabel::NormalizeNet(net);
    if (compared == "0" || compared == "gnd") {
        return 0;
    }
    const auto found = m_NodeOfNet.find(compared);
    if (found == m_NodeOfNet.end()) {
        return std::nullopt;
    }
    return found->second;
}

/**
 * @brief   Returns the names of the nets, for a copy of the circuit.
 * @return  The node of each named net, by the name in the form nets are compared in.
 */
const std::map<std::string, int, std::less<>> &Circuit::GetNets() const {
    return m_NodeOfNet;
}

/**
 * @brief   Returns how many nodes the circuit uses, counting ground.
 * @return  The highest node number plus one, or 0 for an empty circuit.
 */
int Circuit::GetNodeCount() const {
    return m_NodeCount;
}

/**
 * @brief   Tells whether the circuit has a ground reference.
 * @return  True when it, or a subcircuit one of its instances uses, contains a ground component; without one, node
 *          voltages are undefined.
 */
bool Circuit::HasGround() const {
    return std::ranges::any_of(CollectParts(*this),
                               [](const Component *part) { return part->GetType() == ComponentType::GROUND; });
}

/**
 * @brief   Tells whether the circuit has a source that an AC sweep can excite.
 * @return  True when it, or a subcircuit one of its instances uses, contains a voltage or current source set to AC.
 */
bool Circuit::HasACSource() const {
    return std::ranges::any_of(CollectParts(*this), [](const Component *part) { return IsACSource(*part); });
}

/**
 * @brief   Tells whether any part starts from an initial condition.
 * @return  True when at least one capacitor or inductor, here or in a subcircuit one of its instances uses, has one
 *          set; a transient then runs with uic.
 */
bool Circuit::HasInitialConditions() const {
    return std::ranges::any_of(CollectParts(*this), [](const Component *part) { return part->HasInitialCondition(); });
}

/**
 * @brief   Tells whether the circuit has a fuse.
 * @return  True when it, or a subcircuit one of its instances uses, contains a fuse, which needs the Gear integration.
 */
bool Circuit::HasFuse() const {
    return std::ranges::any_of(CollectParts(*this),
                               [](const Component *part) { return part->GetType() == ComponentType::FUSE; });
}

/**
 * @brief   Tells whether the circuit has a thyristor (SCR), a TRIAC, a GTO or a DIAC.
 * @return  True when it, or a subcircuit one of its instances uses, contains one, which needs the Gear integration.
 */
bool Circuit::HasThyristor() const {
    return std::ranges::any_of(CollectParts(*this), [](const Component *part) {
        const ComponentType type = part->GetType();
        return type == ComponentType::THYRISTOR || type == ComponentType::TRIAC || type == ComponentType::GTO
            || type == ComponentType::DIAC;
    });
}

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @param[in] analysis            Optional analysis command, such as ".op", written right before ".end".
 * @param[in] add_current_probes  Adds a 0 V source in series with every terminal GetCurrentProbes() lists; for
 *                                simulation only.
 * @return  The netlist WriteSpiceNetlist() writes, where its lines and notes are described.
 */
std::string Circuit::ToSpiceNetlist(const std::string_view analysis, const bool add_current_probes) const {
    return WriteSpiceNetlist(*this, analysis, add_current_probes);
}

/**
 * @brief   Lists every part the netlist of a circuit simulates.
 * @param[in] circuit  Circuit to look into.
 * @return  The parts of the circuit, then the parts of each subcircuit definition one of its instances uses, at any
 *          depth, once per definition. Definitions no instance uses are left out.
 */
std::vector<const Component *> CollectParts(const Circuit &circuit) {
    std::vector<const Component *> parts;
    std::set<std::string, std::less<>> visited;
    const auto collect = [&](const auto &self, const Circuit &body) -> void {
        for (const CircuitEntry &entry : body.GetEntries()) {
            parts.push_back(entry.Part);
            if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
                continue;
            }
            const std::string &name = static_cast<const SubcircuitInstance &>(*entry.Part).GetDefinitionName();
            if (!visited.insert(name).second) {
                continue;
            }
            if (const auto definition = circuit.FindSubcircuit(name)) {
                self(self, definition->Body);
            }
        }
    };
    collect(collect, circuit);
    return parts;
}

} // namespace Core
