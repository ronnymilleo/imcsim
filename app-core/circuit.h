/**
 * @file    circuit.h
 * @brief   Circuit topology handed to the simulator: components and the nodes their terminals connect to.
 */

#ifndef IMCSIM_CIRCUIT_H
#define IMCSIM_CIRCUIT_H

#include "components/component.h"
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  CircuitEntry
 * @brief   A component of the circuit and the node of each of its terminals.
 * @details The component is not owned: whoever built the circuit must keep it alive while the circuit is used.
 */
struct CircuitEntry {
    const Component *Part = nullptr;
    // One node per terminal, in terminal order; node 0 is ground
    std::vector<int> Nodes;
};

/**
 * @class   Circuit
 * @brief   Topology of a circuit, without geometry: which nodes each component connects.
 */
class Circuit {
public:
    void Add(const Component &component, std::vector<int> nodes);

    const std::vector<CircuitEntry> &GetEntries() const;
    int GetNodeCount() const;
    bool HasGround() const;
    bool HasACSource() const;
    std::string ToSpiceNetlist(std::string_view analysis = "", bool add_current_probes = false) const;

private:
    std::vector<CircuitEntry> m_Entries;
    int m_NodeCount = 0;
};

std::string GetCurrentProbeName(const Component &diode);

} // namespace Core

#endif // IMCSIM_CIRCUIT_H
