/**
 * @file    simulation_request.h
 * @brief   Works out the analysis a simulate call asks for, from its arguments and the directives of the netlist.
 */

#ifndef NODALYS_SIMULATION_REQUEST_H
#define NODALYS_SIMULATION_REQUEST_H

#include "mcp/json_arguments.h"
#include "mcp/node_labels.h"
#include "nodalys/netlist_parser.h"

#include <expected>
#include <string>

namespace Nodalys {

// Requests
[[nodiscard]] std::expected<ParsedAnalysis, std::string>
BuildAnalysis(ArgumentReader &reader, const ParsedNetlist &parsed, const NodeLabels &labels);

} // namespace Nodalys

#endif // NODALYS_SIMULATION_REQUEST_H
