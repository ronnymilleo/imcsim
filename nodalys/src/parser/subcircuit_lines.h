/**
 * @file    subcircuit_lines.h
 * @brief   The grammar of the .subckt and X lines: where their nodes end, which subcircuit an X line places, and the
 *          parameters both may give.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_LINES_H
#define NODALYS_PARSER_SUBCIRCUIT_LINES_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nodalys {

/// The word that may start the parameters of a .subckt or an X line, as in "params: gain=2".
constexpr std::string_view PARAMETER_LIST_MARK = "PARAMS:";

/**
 * @struct  InstanceLine
 * @brief   The parts of an X line: X<name> <nodes> <subcircuit> [params:] [NAME=VALUE...].
 */
struct InstanceLine {
    std::vector<std::string> NodeNames;
    std::string SubcircuitName;
    /// The NAME=VALUE tokens, as written, with the PARAMS: word among them when the line has it.
    std::vector<std::string> ParameterTokens;
};

// Parameters
bool IsParameterStart(const std::string &token);
std::vector<std::pair<std::string, std::string>> SplitParameterTokens(const std::vector<std::string> &tokens,
                                                                      std::size_t first);

// Instances
std::optional<InstanceLine> SplitInstanceLine(const std::vector<std::string> &tokens);

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_LINES_H
