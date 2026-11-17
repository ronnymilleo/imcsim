/**
 * @file    mcp_tools.h
 * @brief   The tools of the Nodalys MCP server.
 */

#ifndef NODALYS_MCP_TOOLS_H
#define NODALYS_MCP_TOOLS_H

#include "mcp/mcp_tool.h"

#include <vector>

namespace Nodalys {

// Tools
std::vector<McpTool> MakeNodalysTools();

} // namespace Nodalys

#endif // NODALYS_MCP_TOOLS_H
