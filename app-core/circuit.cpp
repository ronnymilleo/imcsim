/**
 * @file    circuit.cpp
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#include "circuit.h"

#include "spice_value.h"
#include <algorithm>
#include <format>
#include <utility>

namespace Core {

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
 * @brief   Returns the components of the circuit, in the order they were added.
 * @return  Every component with the nodes of its terminals.
 */
const std::vector<CircuitEntry> &Circuit::GetEntries() const {
    return m_Entries;
}

/**
 * @brief   Returns how many nodes the circuit uses, counting ground.
 * @return  The highest node number plus one, or 0 for an empty circuit.
 */
int Circuit::GetNodeCount() const {
    return m_NodeCount;
}

/**
 * @brief   Writes the circuit as a SPICE netlist, ready to hand to ngspice.
 * @return  One line per component, with node numbers as SPICE node names (0 is ground), ending in ".end".
 * @note    Ground components produce no line; they only make their node 0. A supply rail becomes a DC voltage
 *          source from its node to ground. Analysis commands are left to the caller.
 */
std::string Circuit::ToSpiceNetlist() const {
    std::string netlist = "* imcsim netlist\n";
    for (const CircuitEntry &entry : m_Entries) {
        const Component &component = *entry.Part;
        const std::string value = FormatSpiceValue(component.GetValue());
        switch (component.GetType()) {
        case ComponentType::Resistor:
        case ComponentType::Capacitor:
        case ComponentType::Inductor:
            netlist += std::format("{} {} {} {}\n", component.GetName(), entry.Nodes[0], entry.Nodes[1], value);
            break;
        case ComponentType::VCC:
            netlist += std::format("{} {} 0 DC {}\n", component.GetName(), entry.Nodes[0], value);
            break;
        case ComponentType::Ground:
            break;
        }
    }
    netlist += ".end\n";
    return netlist;
}

} // namespace Core
