/**
 * @file    mcp_tools.cpp
 * @brief   The tools of the Nodalys MCP server.
 */

#include "mcp/mcp_tools.h"

#include "mcp/tool_check_netlist.h"
#include "mcp/tool_list_models.h"
#include "mcp/tool_simulate.h"

namespace Nodalys {

/**
 * @return  The tools the server offers, in the order they are listed: check_netlist, simulate and list_models.
 */
std::vector<McpTool> MakeNodalysTools() {
    return {MakeCheckNetlistTool(), MakeSimulateTool(), MakeListModelsTool()};
}

} // namespace Nodalys
