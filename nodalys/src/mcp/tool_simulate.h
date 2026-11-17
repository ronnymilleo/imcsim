/**
 * @file    tool_simulate.h
 * @brief   The simulate tool: runs one analysis on a netlist and answers compact figures or the full data.
 */

#ifndef NODALYS_TOOL_SIMULATE_H
#define NODALYS_TOOL_SIMULATE_H

#include "mcp/mcp_tool.h"

namespace Nodalys {

// Tool
McpTool MakeSimulateTool();

} // namespace Nodalys

#endif // NODALYS_TOOL_SIMULATE_H
