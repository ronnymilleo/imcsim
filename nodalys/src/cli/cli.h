/**
 * @file    cli.h
 * @brief   The command line of the standalone Nodalys tool: reads a netlist, runs its analyses and writes the results.
 * @details Apart from main(), so the tests run it in their own process, with their own streams.
 */

#ifndef NODALYS_CLI_H
#define NODALYS_CLI_H

#include <istream>
#include <ostream>

namespace Nodalys {

int RunCli(int argc, const char *const *argv, std::istream &input, std::ostream &output, std::ostream &errors);

} // namespace Nodalys

#endif // NODALYS_CLI_H
