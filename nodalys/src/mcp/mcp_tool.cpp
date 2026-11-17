/**
 * @file    mcp_tool.cpp
 * @brief   What an MCP tool is for the server: a name, a description for the AI, a JSON Schema and a function to run.
 */

#include "mcp/mcp_tool.h"

#include <utility>

namespace Nodalys {

/**
 * @brief   Makes a successful result from a JSON value, written compactly to spare the AI's context.
 * @param[in] value  Value to write.
 * @return  The result with the value as text.
 * @note    Text that is not valid UTF-8 is replaced instead of failing, since a netlist title may hold any bytes.
 */
ToolResult MakeJsonResult(const nlohmann::json &value) {
    constexpr int NO_INDENT = -1;
    return {.Text = value.dump(NO_INDENT, ' ', false, nlohmann::json::error_handler_t::replace), .IsError = false};
}

/**
 * @brief   Makes a result that reports a failure of the tool's work.
 * @param[in] message  What went wrong and, when it helps, how to correct the call.
 * @return  The result with `IsError` set.
 */
ToolResult MakeErrorResult(std::string message) {
    return {.Text = std::move(message), .IsError = true};
}

} // namespace Nodalys
