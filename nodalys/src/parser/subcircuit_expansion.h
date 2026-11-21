/**
 * @file    subcircuit_expansion.h
 * @brief   Expands the lines of a subcircuit kept as SPICE text in a scope: its definitions, its elements and the
 *          instances of the subcircuits it defines, each in a scope of its own.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_EXPANSION_H
#define NODALYS_PARSER_SUBCIRCUIT_EXPANSION_H

#include "parser/classified_lines.h"
#include "parser/subcircuit_scope.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

// Expansion
[[nodiscard]] std::expected<void, std::string> ExpandText(TextScope &scope, const std::vector<std::string> &lines);
[[nodiscard]] std::expected<void, std::string>
ExpandTextInstance(TextScope &scope, const std::vector<std::string> &definitions, const TextElement &call);

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_EXPANSION_H
