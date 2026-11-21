/**
 * @file    netlist_subcircuits.h
 * @brief   Places the subcircuits of a netlist: the X lines, over the scoped reader of subcircuits kept as text.
 */

#ifndef NODALYS_PARSER_NETLIST_SUBCIRCUITS_H
#define NODALYS_PARSER_NETLIST_SUBCIRCUITS_H

#include "nodalys/circuit_builder.h"
#include "nodalys/types.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  InstanceObservers
 * @brief   What the caller wants to hear about the nodes and sources that the instances of a netlist make.
 */
struct InstanceObservers {
    std::function<void(const std::string &name, NodeId node)> OnInternalNode;
    std::function<void(const std::string &name, NodeId positive, NodeId negative)> OnSource;
};

// Placing
std::expected<std::vector<std::string>, std::string>
AddNetlistInstance(CircuitBuilder &builder,
                   const std::vector<std::string> &definition_lines,
                   std::string_view line,
                   const std::function<NodeId(std::string_view)> &resolve_node,
                   const InstanceObservers &observers);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_SUBCIRCUITS_H
