/**
 * @file    subcircuit_lines.cpp
 * @brief   The grammar of the .subckt and X lines: where their nodes end, which subcircuit an X line places, and the
 *          parameters both may give.
 */

#include "parser/subcircuit_lines.h"

#include "text/text_case.h"

namespace Nodalys {

namespace {

/// The first node of an X line comes right after its name.
constexpr std::size_t FIRST_NODE_INDEX = 1;

} // namespace

/**
 * @brief   Tells whether a token starts the parameters of a .subckt or an X line: the PARAMS: word or a NAME=VALUE.
 * @param[in] token  Token of the line, after its nodes.
 */
bool IsParameterStart(const std::string &token) {
    return ToUpper(token).starts_with(PARAMETER_LIST_MARK)
        || (token.find('=') != std::string::npos && token.front() != '{' && token.front() != '\'');
}

/**
 * @brief   Splits the NAME=VALUE tokens that follow the nodes of a line, skipping the PARAMS: word.
 * @param[in] tokens  The tokens of the line.
 * @param[in] first   Index of the first parameter token.
 * @return  The upper-case names with the values as written; a name without a value has an empty one.
 */
std::vector<std::pair<std::string, std::string>> SplitParameterTokens(const std::vector<std::string> &tokens,
                                                                      const std::size_t first) {
    std::vector<std::pair<std::string, std::string>> parameters;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        std::string token = tokens[index];
        if (ToUpper(token).starts_with(PARAMETER_LIST_MARK)) {
            token.erase(0, PARAMETER_LIST_MARK.size());
        }
        if (token.empty()) {
            continue;
        }
        const std::size_t equals = token.find('=');
        parameters.emplace_back(ToUpper(token.substr(0, equals)),
                                equals == std::string::npos ? std::string() : token.substr(equals + 1));
    }
    return parameters;
}

/**
 * @brief   Splits an X line into its nodes, the subcircuit it calls and its parameters.
 * @param[in] tokens  The tokens of the line, as SplitTokens() gives them.
 * @return  The parts, or none when the line is not written X<name> <nodes> <subcircuit>.
 */
std::optional<InstanceLine> SplitInstanceLine(const std::vector<std::string> &tokens) {
    std::size_t parameters_start = FIRST_NODE_INDEX + 1;
    while (parameters_start < tokens.size() && !IsParameterStart(tokens[parameters_start])) {
        ++parameters_start;
    }
    // At least the name of the subcircuit has to sit between the name of the instance and the parameters
    if (parameters_start <= FIRST_NODE_INDEX + 1) {
        return std::nullopt;
    }
    InstanceLine instance;
    const auto nodes_end = tokens.begin() + static_cast<std::ptrdiff_t>(parameters_start - 1);
    instance.NodeNames.assign(tokens.begin() + static_cast<std::ptrdiff_t>(FIRST_NODE_INDEX), nodes_end);
    instance.SubcircuitName = *nodes_end;
    instance.ParameterTokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(parameters_start), tokens.end());
    return instance;
}

} // namespace Nodalys
