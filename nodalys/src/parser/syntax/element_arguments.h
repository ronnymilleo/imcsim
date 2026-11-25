/**
 * @file    element_arguments.h
 * @brief   Reads the generic parts of an element line of a subcircuit kept as SPICE text: its nodes, its bare and
 *          NAME=VALUE arguments, and the checks that refuse what the element has no use for.
 */

#ifndef NODALYS_PARSER_ELEMENT_ARGUMENTS_H
#define NODALYS_PARSER_ELEMENT_ARGUMENTS_H

#include "nodalys/circuit_builder.h"
#include "nodalys/types.h"
#include "parser/subcircuit/subcircuit_scope.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/// The index of the first node of an element line, after its name.
constexpr std::size_t FIRST_NODE_INDEX = 1;
/// The index of the first word after the two nodes of a two-terminal element, where its value starts.
constexpr std::size_t TWO_TERMINAL_VALUE_INDEX = 3;

/// Evaluates one option value text (`(key, raw_value)`) into a number or an error message.
using OptionEvaluator = std::function<std::expected<double, std::string>(std::string_view key, std::string_view text)>;

/**
 * @struct  ElementLine
 * @brief   An element line as the handlers read it.
 */
struct ElementLine {
    /// The scope the line is in, which evaluates its values, names its nodes and leads to the expansion.
    TextScope &Scope;
    std::string_view Text;
    const std::vector<std::string> &Tokens;
    /// The element letter in upper case, such as 'V', which tells a handler that reads two letters which one it has.
    char Letter;
    std::string Name; ///< The name the element has in the circuit, with the instance it sits in.

    NodeId ResolveNode(std::size_t position) const;
    CircuitBuilder &GetBuilder() const;
};

/**
 * @struct  Arguments
 * @brief   The tokens of an element after its nodes: those written as a bare value or word, and the NAME=VALUE ones.
 */
struct Arguments {
    std::vector<std::string> Positional;
    std::map<std::string, std::string> Options;
};

// Reading
[[nodiscard]] std::expected<TransmissionLineParameters, std::string>
ParseTransmissionLineOptions(const Arguments &arguments, const OptionEvaluator &evaluate);
Arguments SplitArguments(const std::vector<std::string> &tokens, std::size_t first);

// Checks
[[nodiscard]] std::expected<void, std::string> CheckOptions(const Arguments &arguments,
                                                            std::initializer_list<std::string_view> allowed);
[[nodiscard]] std::expected<void, std::string> CheckPositionalCount(const Arguments &arguments, std::size_t most);
[[nodiscard]] std::expected<void, std::string> CheckNodeCount(const ElementLine &element, std::size_t count);

} // namespace Nodalys

#endif // NODALYS_PARSER_ELEMENT_ARGUMENTS_H
