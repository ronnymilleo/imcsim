/**
 * @file    tool_list_models.h
 * @brief   The list_models tool: what devices, sources, models, directives and analyses the netlist reader accepts.
 */

#ifndef NODALYS_TOOL_LIST_MODELS_H
#define NODALYS_TOOL_LIST_MODELS_H

#include "mcp/mcp_tool.h"

namespace Nodalys {

// Tool
McpTool MakeListModelsTool();

} // namespace Nodalys

#endif // NODALYS_TOOL_LIST_MODELS_H
