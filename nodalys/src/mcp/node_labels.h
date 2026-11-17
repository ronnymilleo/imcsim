/**
 * @file    node_labels.h
 * @brief   Names the nodes of a parsed netlist for the AI, and finds a node from the name it wrote.
 */

#ifndef NODALYS_NODE_LABELS_H
#define NODALYS_NODE_LABELS_H

#include "nodalys/netlist_parser.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @class   NodeLabels
 * @brief   The names of the nodes of a netlist, in lower case as the tools write them.
 * @details The results hold voltages by node number; the AI wrote names such as `out`, so every answer goes through
 *          this table. Ground is "0".
 */
class NodeLabels {
public:
    explicit NodeLabels(const ParsedNetlist &parsed);

    // Names
    std::string GetName(NodeId node) const;
    [[nodiscard]] std::optional<NodeId> FindNode(std::string_view name) const;

    // Lists
    std::vector<NodeId> GetNamedNodes() const;

private:
    // Names
    std::map<std::string, NodeId> m_NodesByName;
    std::map<NodeId, std::string> m_NamesByNode;
};

} // namespace Nodalys

#endif // NODALYS_NODE_LABELS_H
