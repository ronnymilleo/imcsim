/**
 * @file    element_arguments.cpp
 * @brief   Reads the generic parts of an element line of a subcircuit kept as SPICE text: its nodes, its bare and
 *          NAME=VALUE arguments, and the checks that refuse what the element has no use for.
 */

#include "parser/syntax/element_arguments.h"

#include "text/text_case.h"

#include <algorithm>
#include <format>

namespace Nodalys {

/**
 * @brief   The node at a position of the line, counted from its first node, which the scope makes when it is new.
 * @param[in] position  Position among the nodes, 0 for the first.
 * @return  The node.
 */
NodeId ElementLine::ResolveNode(const std::size_t position) const {
    return Scope.ResolveNode(Tokens[FIRST_NODE_INDEX + position]);
}

/**
 * @brief   The builder of the expansion the line is added to.
 */
CircuitBuilder &ElementLine::GetBuilder() const {
    return Scope.GetContext().Builder;
}

/**
 * @brief   Sorts the tokens of an element from a position on into bare words and NAME=VALUE options.
 * @details A token is an option when its equals sign comes before any parenthesis and it is no braced or quoted
 *          expression, so "{A=B}" and "SIN(0 1 F=2)" stay bare.
 * @param[in] tokens  Tokens of the line.
 * @param[in] first   Index of the first token to sort.
 * @return  The bare words in order, and the options by upper-case name.
 */
Arguments SplitArguments(const std::vector<std::string> &tokens, const std::size_t first) {
    Arguments arguments;
    for (std::size_t index = first; index < tokens.size(); ++index) {
        const std::size_t equals = tokens[index].find('=');
        const bool is_option = equals != std::string::npos && equals > 0 && tokens[index].front() != '{'
                            && tokens[index].front() != '\'' && tokens[index].find('(') > equals;
        if (is_option) {
            arguments.Options[ToUpper(tokens[index].substr(0, equals))] = tokens[index].substr(equals + 1);
        } else {
            arguments.Positional.push_back(tokens[index]);
        }
    }
    return arguments;
}

/**
 * @brief   Refuses an option the element has no use for, so that none is dropped without a word.
 * @param[in] arguments  Arguments of the element.
 * @param[in] allowed    Upper-case names of the options the element reads.
 * @return  Nothing, or the first option it does not read.
 */
std::expected<void, std::string> CheckOptions(const Arguments &arguments,
                                              const std::initializer_list<std::string_view> allowed) {
    for (const auto &[key, value] : arguments.Options) {
        if (std::ranges::find(allowed, key) == allowed.end()) {
            return std::unexpected(std::format("the option {}= is not simulated", key));
        }
    }
    return {};
}

/**
 * @brief   Refuses a bare word past those the element reads.
 * @param[in] arguments  Arguments of the element.
 * @param[in] most       How many bare words the element reads.
 * @return  Nothing, or the first word too many.
 */
std::expected<void, std::string> CheckPositionalCount(const Arguments &arguments, const std::size_t most) {
    if (arguments.Positional.size() > most) {
        return std::unexpected(std::format("'{}' is not understood here", arguments.Positional[most]));
    }
    return {};
}

/**
 * @brief   Refuses a line with fewer nodes than its element needs.
 * @param[in] element  The element line.
 * @param[in] count    How many nodes the element needs.
 * @return  Nothing, or how many nodes it needs.
 */
std::expected<void, std::string> CheckNodeCount(const ElementLine &element, const std::size_t count) {
    if (element.Tokens.size() < FIRST_NODE_INDEX + count) {
        return std::unexpected(std::format("it needs {} nodes", count));
    }
    return {};
}

} // namespace Nodalys
