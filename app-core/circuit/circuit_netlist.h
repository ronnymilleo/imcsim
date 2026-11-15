/**
 * @file    circuit_netlist.h
 * @brief   Writes a circuit as a SPICE netlist and names the elements each part becomes in it.
 */

#ifndef IMCSIM_CIRCUIT_NETLIST_H
#define IMCSIM_CIRCUIT_NETLIST_H

#include "circuit/circuit.h"
#include "circuit/components/component.h"
#include <string>
#include <string_view>
#include <vector>

namespace Core {

std::string WriteSpiceNetlist(const Circuit &circuit, std::string_view analysis = "", bool add_current_probes = false);
std::vector<std::string> GetElementNames(const Component &component);

} // namespace Core

#endif // IMCSIM_CIRCUIT_NETLIST_H
