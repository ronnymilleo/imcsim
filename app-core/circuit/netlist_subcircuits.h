/**
 * @file    netlist_subcircuits.h
 * @brief   Writes the .subckt block of each block definition, its models scoped to it, in the order of use.
 */

#ifndef IMCSIM_NETLIST_SUBCIRCUITS_H
#define IMCSIM_NETLIST_SUBCIRCUITS_H

#include "circuit/circuit.h"
#include "circuit/subcircuit.h"
#include <memory>
#include <string>
#include <vector>

namespace Core {

std::string FormatSubcircuit(const SubcircuitDefinition &definition);
std::vector<const SubcircuitDefinition *> OrderByUse(const std::vector<std::shared_ptr<SubcircuitDefinition>> &list);

} // namespace Core

#endif // IMCSIM_NETLIST_SUBCIRCUITS_H
