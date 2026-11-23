/**
 * @file    subcircuit_elements.h
 * @brief   Adds one element line of a subcircuit kept as SPICE text to the circuit being built.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_ELEMENTS_H
#define NODALYS_PARSER_SUBCIRCUIT_ELEMENTS_H

#include "parser/subcircuit/subcircuit_scope.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

// Elements
[[nodiscard]] std::expected<void, std::string>
AddTextElement(TextScope &scope, std::string_view line, const std::vector<std::string> &tokens);

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_ELEMENTS_H
