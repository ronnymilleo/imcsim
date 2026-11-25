/**
 * @file    netlist_subcircuits.h
 * @brief   Places the subcircuits of a netlist: the X lines, over the scoped reader of subcircuits kept as text.
 */

#ifndef NODALYS_PARSER_NETLIST_SUBCIRCUITS_H
#define NODALYS_PARSER_NETLIST_SUBCIRCUITS_H

#include "nodalys/circuit_builder.h"
#include "nodalys/subcircuit_text.h"
#include "nodalys/types.h"
#include "parser/netlist/netlist_deck.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

// Placing
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
AddNetlistInstance(CircuitBuilder &builder,
                   const std::vector<std::string> &definition_lines,
                   const DeckLine &line,
                   const std::function<NodeId(std::string_view)> &resolve_node,
                   const InstanceObservers &observers);

} // namespace Nodalys

#endif // NODALYS_PARSER_NETLIST_SUBCIRCUITS_H
