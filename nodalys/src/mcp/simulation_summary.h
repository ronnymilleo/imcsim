/**
 * @file    simulation_summary.h
 * @brief   Turns the result of an analysis into the compact figures of an MCP answer, or into its full data.
 */

#ifndef NODALYS_SIMULATION_SUMMARY_H
#define NODALYS_SIMULATION_SUMMARY_H

#include "mcp/node_labels.h"
#include "nodalys/results.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace Nodalys {

// Summaries
nlohmann::json SummarizeResult(const SimulationResult &result, const NodeLabels &labels,
                               const std::vector<NodeId> &nodes);

// Full data
nlohmann::json MakeFullData(const SimulationResult &result, const NodeLabels &labels);

} // namespace Nodalys

#endif // NODALYS_SIMULATION_SUMMARY_H
