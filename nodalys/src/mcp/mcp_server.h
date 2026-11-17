/**
 * @file    mcp_server.h
 * @brief   The MCP server of Nodalys: reads JSON-RPC messages line by line and answers on a stream.
 */

#ifndef NODALYS_MCP_SERVER_H
#define NODALYS_MCP_SERVER_H

#include <istream>
#include <ostream>

namespace Nodalys {

// Serving
int RunMcpServer(std::istream &input, std::ostream &output, std::ostream &errors);

} // namespace Nodalys

#endif // NODALYS_MCP_SERVER_H
