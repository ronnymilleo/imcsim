/**
 * @file    netlist_nodes.h
 * @brief   The node names of a netlist and the numbers the engine gives them.
 */

#ifndef NODALYS_PARSER_NETLIST_NODES_H
#define NODALYS_PARSER_NETLIST_NODES_H

#include "nodalys/types.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nodalys {

/**
 * @class   NetlistNodes
 * @brief   Numbers the nodes of a netlist: a node named by a number keeps it, and the named ones follow the highest.
 * @details Every node is registered first, then AssignIds() numbers them all at once, so a named node never takes a
 *          number that a numbered node of a later line uses. Names are read without regard to case.
 */
class NetlistNodes {
public:
    void Register(std::string_view name);
    int AssignIds();
    void Bind(const std::string &upper_name, NodeId node);
    NodeId Resolve(std::string_view name) const;

private:
    std::set<std::string> m_Registered;
    std::vector<std::pair<std::string, int>> m_NumericNodes;
    std::vector<std::string> m_SymbolicNodes;
    int m_MaxNumeric = 0;
    std::map<std::string, NodeId> m_NodeMap;
};

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_NODES_H
