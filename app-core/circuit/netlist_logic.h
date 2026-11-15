/**
 * @file    netlist_logic.h
 * @brief   Writes the netlist lines of a logic part: its digital model, its bridges and the elements they make.
 */

#ifndef IMCSIM_NETLIST_LOGIC_H
#define IMCSIM_NETLIST_LOGIC_H

#include "circuit/components/component.h"
#include "circuit/components/logic_part.h"
#include <string>
#include <vector>

namespace Core {

std::string FormatLogicModels(const LogicPart &part);
std::string FormatLogicPart(const LogicPart &part, const std::vector<int> &nodes, int &next_extra_node);
std::vector<std::string> GetLogicElementNames(const Component &part);

} // namespace Core

#endif // IMCSIM_NETLIST_LOGIC_H
