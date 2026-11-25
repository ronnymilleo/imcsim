/**
 * @file    expression_references.h
 * @brief   Writes the voltages and currents an expression reads with the names the builder knows, for a behavioral
 *          source of a netlist or of a subcircuit kept as text.
 */

#ifndef NODALYS_PARSER_EXPRESSION_REFERENCES_H
#define NODALYS_PARSER_EXPRESSION_REFERENCES_H

#include "nodalys/expression.h"
#include "nodalys/types.h"

#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace Nodalys {

/// The number of a node an expression names, as the netlist or the scope of a subcircuit resolves it.
using ExpressionNodeResolver = std::function<NodeId(std::string_view)>;
/// The name of the voltage source whose current an expression reads, or the reason it names none.
using ExpressionSourceResolver = std::function<std::expected<std::string, std::string>(std::string_view)>;

// References
[[nodiscard]] std::expected<std::string, std::string>
RewriteExpressionReferences(std::string_view text,
                            const Expression &expression,
                            const ExpressionNodeResolver &resolve_node,
                            const ExpressionSourceResolver &resolve_source);

} // namespace Nodalys

#endif // NODALYS_PARSER_EXPRESSION_REFERENCES_H
