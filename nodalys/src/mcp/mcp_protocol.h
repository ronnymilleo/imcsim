/**
 * @file    mcp_protocol.h
 * @brief   The Model Context Protocol over JSON-RPC 2.0: the handshake, the tool calls and the errors, apart from the
 *          tools themselves.
 */

#ifndef NODALYS_MCP_PROTOCOL_H
#define NODALYS_MCP_PROTOCOL_H

#include "mcp/mcp_tool.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @class   McpProtocol
 * @brief   Answers the JSON-RPC messages of an MCP client, one message at a time.
 * @details Speaks the tools capability only: `initialize`, `notifications/initialized`, `ping`, `tools/list` and
 *          `tools/call`. It keeps no state about a session, so a client that skips the handshake is still served.
 */
class McpProtocol {
public:
    McpProtocol(std::string server_name, std::string server_version, std::string instructions,
                std::vector<McpTool> tools);

    // Messages
    std::optional<std::string> HandleMessage(std::string_view message) const;

private:
    // Requests
    nlohmann::json HandleRequest(const nlohmann::json &request, const nlohmann::json &id) const;
    nlohmann::json HandleInitialize(const nlohmann::json &params, const nlohmann::json &id) const;
    nlohmann::json HandleToolsList(const nlohmann::json &id) const;
    nlohmann::json HandleToolsCall(const nlohmann::json &params, const nlohmann::json &id) const;

    // Tools
    const McpTool *FindTool(std::string_view name) const;

    // Server
    std::string m_ServerName;
    std::string m_ServerVersion;
    std::string m_Instructions;
    std::vector<McpTool> m_Tools;
};

} // namespace Nodalys

#endif // NODALYS_MCP_PROTOCOL_H
