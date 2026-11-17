/**
 * @file    mcp_tool.h
 * @brief   What an MCP tool is for the server: a name, a description for the AI, a JSON Schema and a function to run.
 */

#ifndef NODALYS_MCP_TOOL_H
#define NODALYS_MCP_TOOL_H

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace Nodalys {

/**
 * @struct  ToolResult
 * @brief   What a tool answers: a text for the AI and whether it is the report of a failure.
 * @details A failure of the tool's own work (a bad netlist, a run that does not converge) is a result with `IsError`
 *          set, not a protocol error, so the AI reads the message and can correct its call.
 */
struct ToolResult {
    std::string Text;
    bool IsError = false;
};

/**
 * @struct  McpTool
 * @brief   One tool the server offers.
 */
struct McpTool {
    std::string Name;
    /// Written for an AI reader: what the tool does, when to use it and what it answers.
    std::string Description;
    /// JSON Schema of the arguments object.
    nlohmann::json InputSchema;
    /// Runs the tool on its arguments object, which the server has checked to be an object.
    std::function<ToolResult(const nlohmann::json &arguments)> Run;
};

// Making results
ToolResult MakeJsonResult(const nlohmann::json &value);
ToolResult MakeErrorResult(std::string message);

} // namespace Nodalys

#endif // NODALYS_MCP_TOOL_H
