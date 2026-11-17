/**
 * @file    analysis_names.cpp
 * @brief   The short names the MCP tools use for the analyses: "op", "tran", "ac", "sens_ac"...
 */

#include "mcp/analysis_names.h"

#include <array>
#include <utility>

namespace Nodalys {

namespace {

/// Every analysis with the name the tools use; the DC and the AC sensitivity are two names.
constexpr std::array<std::pair<std::string_view, AnalysisKind>, 11> ANALYSES = {{
    {"op", AnalysisKind::OPERATING_POINT},
    {"tran", AnalysisKind::TRANSIENT},
    {"ac", AnalysisKind::AC_SWEEP},
    {"dc", AnalysisKind::DC_SWEEP},
    {"noise", AnalysisKind::NOISE},
    {"tf", AnalysisKind::TRANSFER_FUNCTION},
    {"sens", AnalysisKind::SENSITIVITY},
    {"sens_ac", AnalysisKind::AC_SENSITIVITY},
    {"pz", AnalysisKind::POLE_ZERO},
    {"sp", AnalysisKind::S_PARAMETERS},
    {"hb", AnalysisKind::HARMONIC_BALANCE},
}};

constexpr std::array<std::string_view, 11> NAMES = {"op",   "tran",    "ac", "dc", "noise", "tf",
                                                    "sens", "sens_ac", "pz", "sp", "hb"};

} // namespace

/**
 * @return  The names of all the analyses, in the order the documentation lists them.
 */
std::span<const std::string_view> GetAnalysisNames() {
    return NAMES;
}

/**
 * @param[in] kind  Analysis kind.
 * @return  Its short name, such as "tran".
 */
std::string_view GetAnalysisName(const AnalysisKind kind) {
    for (const auto &[name, candidate] : ANALYSES) {
        if (candidate == kind) {
            return name;
        }
    }
    return "unknown";
}

/**
 * @param[in] name  Short name, in lower case.
 * @return  The analysis it names, or none for an unknown name.
 */
std::optional<AnalysisKind> FindAnalysisKind(const std::string_view name) {
    for (const auto &[candidate_name, kind] : ANALYSES) {
        if (candidate_name == name) {
            return kind;
        }
    }
    return std::nullopt;
}

} // namespace Nodalys
