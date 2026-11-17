/**
 * @file    mcp_protocol.cpp
 * @brief   The Model Context Protocol over JSON-RPC 2.0: the handshake, the tool calls and the errors, apart from the
 *          tools themselves.
 */

#include "mcp/mcp_protocol.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

// Codes of JSON-RPC 2.0 for the failures of the protocol itself
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;

/// Revisions of the MCP specification the server speaks, the newest first. A client's version is answered with itself
/// when listed here, and with the newest one otherwise, as the specification asks.
constexpr std::array<std::string_view, 4> SUPPORTED_PROTOCOL_VERSIONS = {"2025-11-25", "2025-06-18", "2025-03-26",
                                                                         "2024-11-05"};

nlohmann::json MakeError(const nlohmann::json &id, const int code, const std::string &message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json MakeResponse(const nlohmann::json &id, nlohmann::json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

/// Whether the id of a request has the type JSON-RPC allows: a string or a number, never null for a request here.
bool IsValidId(const nlohmann::json &id) {
    return id.is_string() || id.is_number();
}

} // namespace

/**
 * @param[in] server_name     Name sent in `serverInfo`.
 * @param[in] server_version  Version sent in `serverInfo`.
 * @param[in] instructions    Text for the AI on how to use the server as a whole, sent in `initialize`.
 * @param[in] tools           Tools the server offers.
 */
McpProtocol::McpProtocol(std::string server_name, std::string server_version, std::string instructions,
                         std::vector<McpTool> tools)
    : m_ServerName(std::move(server_name)), m_ServerVersion(std::move(server_version)),
      m_Instructions(std::move(instructions)), m_Tools(std::move(tools)) {
}

/**
 * @brief   Answers one message of the client.
 * @param[in] message  One JSON-RPC message as text, without its line break.
 * @return  The response as one line of JSON, or none for a notification, which gets no answer.
 * @note    A message that is not valid JSON gets a parse error with a null id; a batch (an array) gets an invalid
 *          request error, since the revisions after 2025-03-26 dropped batches.
 */
std::optional<std::string> McpProtocol::HandleMessage(const std::string_view message) const {
    const nlohmann::json request = nlohmann::json::parse(message, nullptr, false);
    nlohmann::json response;

    if (request.is_discarded()) {
        response = MakeError(nullptr, PARSE_ERROR, "Parse error: the message is not valid JSON");
    } else if (!request.is_object()) {
        response = MakeError(nullptr, INVALID_REQUEST, "Invalid request: expected one JSON-RPC object, not a batch");
    } else if (!request.contains("method") || !request["method"].is_string()) {
        // A response from the client, or a message with no method, has nothing to answer
        const bool is_client_response = request.contains("result") || request.contains("error");
        if (is_client_response) {
            return std::nullopt;
        }
        response = MakeError(request.value("id", nlohmann::json(nullptr)), INVALID_REQUEST,
                             "Invalid request: 'method' is missing or not a string");
    } else if (!request.contains("id")) {
        // A notification, such as notifications/initialized or notifications/cancelled: nothing to answer
        return std::nullopt;
    } else if (!IsValidId(request["id"])) {
        response = MakeError(nullptr, INVALID_REQUEST, "Invalid request: 'id' must be a string or a number");
    } else {
        response = HandleRequest(request, request["id"]);
    }
    return response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

nlohmann::json McpProtocol::HandleRequest(const nlohmann::json &request, const nlohmann::json &id) const {
    const std::string method = request["method"].get<std::string>();
    const nlohmann::json params = request.contains("params") ? request["params"] : nlohmann::json::object();

    if (method == "ping") {
        return MakeResponse(id, nlohmann::json::object());
    }
    if (method == "initialize") {
        return HandleInitialize(params, id);
    }
    if (method == "tools/list") {
        return HandleToolsList(id);
    }
    if (method == "tools/call") {
        return HandleToolsCall(params, id);
    }
    return MakeError(id, METHOD_NOT_FOUND, "Method not found: " + method);
}

nlohmann::json McpProtocol::HandleInitialize(const nlohmann::json &params, const nlohmann::json &id) const {
    if (!params.is_object()) {
        return MakeError(id, INVALID_PARAMS, "Invalid params: expected an object");
    }
    std::string version{SUPPORTED_PROTOCOL_VERSIONS.front()};
    if (params.contains("protocolVersion") && params["protocolVersion"].is_string()) {
        const std::string requested = params["protocolVersion"].get<std::string>();
        if (std::ranges::find(SUPPORTED_PROTOCOL_VERSIONS, requested) != SUPPORTED_PROTOCOL_VERSIONS.end()) {
            version = requested;
        }
    }
    nlohmann::json result = {
        {"protocolVersion", version},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo", {{"name", m_ServerName}, {"version", m_ServerVersion}}},
    };
    if (!m_Instructions.empty()) {
        result["instructions"] = m_Instructions;
    }
    return MakeResponse(id, std::move(result));
}

nlohmann::json McpProtocol::HandleToolsList(const nlohmann::json &id) const {
    nlohmann::json tools = nlohmann::json::array();
    for (const McpTool &tool : m_Tools) {
        tools.push_back({{"name", tool.Name},
                         {"description", tool.Description},
                         {"inputSchema", tool.InputSchema},
                         {"annotations", {{"openWorldHint", false}}}});
    }
    return MakeResponse(id, {{"tools", std::move(tools)}});
}

nlohmann::json McpProtocol::HandleToolsCall(const nlohmann::json &params, const nlohmann::json &id) const {
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
        return MakeError(id, INVALID_PARAMS, "Invalid params: 'name' must be the name of a tool");
    }
    const std::string name = params["name"].get<std::string>();
    const McpTool *tool = FindTool(name);
    if (tool == nullptr) {
        return MakeError(id, INVALID_PARAMS, "Unknown tool: " + name);
    }
    const nlohmann::json arguments =
        params.contains("arguments") && !params["arguments"].is_null() ? params["arguments"] : nlohmann::json::object();
    if (!arguments.is_object()) {
        return MakeError(id, INVALID_PARAMS, "Invalid params: 'arguments' must be an object");
    }

    // The tools report their own failures as results; an exception here is a bug that must not end the session
    try {
        const ToolResult tool_result = tool->Run(arguments);
        return MakeResponse(
            id, {{"content", {{{"type", "text"}, {"text", tool_result.Text}}}}, {"isError", tool_result.IsError}});
    } catch (const std::exception &error) {
        return MakeError(id, INTERNAL_ERROR, std::string("Internal error in tool ") + name + ": " + error.what());
    }
}

const McpTool *McpProtocol::FindTool(const std::string_view name) const {
    const auto found = std::ranges::find(m_Tools, name, &McpTool::Name);
    return found != m_Tools.end() ? &*found : nullptr;
}

} // namespace Nodalys
