/**
 * @file    expression_references.cpp
 * @brief   Writes the voltages and currents an expression reads with the names the builder knows, for a behavioral
 *          source of a netlist or of a subcircuit kept as text.
 */

#include "parser/syntax/expression_references.h"

#include <algorithm>
#include <format>

namespace Nodalys {

/**
 * @brief   Rewrites every v(...) and i(...) of an expression, so the builder reads its nodes by number and its
 *          currents by the full name of the source.
 * @param[in] text            The expression, as parsed into expression.
 * @param[in] expression      The parse of text, whose references point into it.
 * @param[in] resolve_node    Gives the number of a node by its name.
 * @param[in] resolve_source  Gives the name of the voltage source whose current is read, or why there is none.
 * @return  The text with each reference written as v(n), v(n,m) or i(name), the rest as it was; the reason a current
 *          cannot be read otherwise.
 */
std::expected<std::string, std::string> RewriteExpressionReferences(const std::string_view text,
                                                                    const Expression &expression,
                                                                    const ExpressionNodeResolver &resolve_node,
                                                                    const ExpressionSourceResolver &resolve_source) {
    std::string resolved;
    std::size_t copied = 0;
    for (const ExpressionReference &reference : expression.GetReferences()) {
        resolved.append(text.substr(copied, reference.Start - copied));
        copied = reference.Start + reference.Length;
        const ExpressionInput &input = reference.Input;
        if (input.Type == ExpressionInput::Kind::CURRENT) {
            const auto source = resolve_source(input.First);
            if (!source) {
                return std::unexpected(source.error());
            }
            resolved += std::format("i({})", *source);
        } else if (input.Second.empty()) {
            resolved += std::format("v({})", resolve_node(input.First));
        } else {
            resolved += std::format("v({},{})", resolve_node(input.First), resolve_node(input.Second));
        }
    }
    resolved.append(text.substr(std::min(copied, text.size())));
    return resolved;
}

} // namespace Nodalys
