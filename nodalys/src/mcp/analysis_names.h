/**
 * @file    analysis_names.h
 * @brief   The short names the MCP tools use for the analyses: "op", "tran", "ac", "sens_ac"...
 */

#ifndef NODALYS_ANALYSIS_NAMES_H
#define NODALYS_ANALYSIS_NAMES_H

#include "nodalys/netlist_parser.h"

#include <optional>
#include <span>
#include <string_view>

namespace Nodalys {

// Names
std::span<const std::string_view> GetAnalysisNames();
std::string_view GetAnalysisName(AnalysisKind kind);
[[nodiscard]] std::optional<AnalysisKind> FindAnalysisKind(std::string_view name);

} // namespace Nodalys

#endif // NODALYS_ANALYSIS_NAMES_H
