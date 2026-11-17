/**
 * @file    tool_check_netlist.h
 * @brief   The check_netlist tool: reads a netlist and reports its mistakes or a short summary of it.
 */

#ifndef NODALYS_TOOL_CHECK_NETLIST_H
#define NODALYS_TOOL_CHECK_NETLIST_H

#include "mcp/mcp_tool.h"

namespace Nodalys {

// Tool
McpTool MakeCheckNetlistTool();

} // namespace Nodalys

#endif // NODALYS_TOOL_CHECK_NETLIST_H
