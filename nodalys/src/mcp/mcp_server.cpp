/**
 * @file    mcp_server.cpp
 * @brief   The MCP server of Nodalys: reads JSON-RPC messages line by line and answers on a stream.
 */

#include "mcp/mcp_server.h"

#include "mcp/mcp_protocol.h"
#include "mcp/mcp_tools.h"
#include "nodalys/version.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>

namespace Nodalys {

namespace {

/// What the AI reads on connecting, before it looks at any tool.
constexpr const char *SERVER_INSTRUCTIONS =
    "Nodalys simulates SPICE netlists. Write a netlist whose first line is a title, then use check_netlist to find "
    "mistakes, simulate to run an analysis and list_models to see what the netlist reader accepts. Values take SPICE "
    "scale suffixes (1k, 4.7u, 100n, 1meg). Answers are compact figures; ask for the full data only when needed.";

} // namespace

/**
 * @brief   Serves MCP over a pair of streams until the input ends.
 * @details Follows the stdio transport: each message is one line of JSON, with no line break inside it, and so is each
 *          answer. Nothing but answers goes to the output; the notes for the person who runs the server go to the
 *          error stream.
 * @param[in,out] input   Stream the client's messages are read from.
 * @param[in,out] output  Stream the answers are written to, flushed after each one.
 * @param[in,out] errors  Stream of the log lines.
 * @return  EXIT_SUCCESS when the input ended.
 */
int RunMcpServer(std::istream &input, std::ostream &output, std::ostream &errors) {
    const McpProtocol protocol("nodalys", NODALYS_VERSION_STRING, SERVER_INSTRUCTIONS, MakeNodalysTools());
    errors << "nodalys mcp: serving on standard input and output\n";

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        if (const std::optional<std::string> answer = protocol.HandleMessage(line)) {
            output << *answer << '\n' << std::flush;
        }
    }
    return EXIT_SUCCESS;
}

} // namespace Nodalys
