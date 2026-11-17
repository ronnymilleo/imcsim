/**
 * @file    analysis_runner.h
 * @brief   Runs one analysis of a parsed netlist, whichever its kind, for the command line and the MCP server.
 */

#ifndef NODALYS_ANALYSIS_RUNNER_H
#define NODALYS_ANALYSIS_RUNNER_H

#include "nodalys/nodalys.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

// Running
[[nodiscard]] std::expected<SimulationResult, std::string>
RunAnalysis(const ParsedNetlist &parsed, const ParsedAnalysis &analysis, std::vector<DiagnosticMessage> *diagnostics);

// Describing
std::string DescribeAnalysis(const ParsedAnalysis &analysis);

} // namespace Nodalys

#endif // NODALYS_ANALYSIS_RUNNER_H
