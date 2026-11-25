/**
 * @file    subcircuit_element_sources.h
 * @brief   Independent, controlled, and behavioral source element handlers for subcircuits kept as SPICE text.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_ELEMENT_SOURCES_H
#define NODALYS_PARSER_SUBCIRCUIT_ELEMENT_SOURCES_H

#include "parser/syntax/element_arguments.h"
#include <expected>
#include <string>

namespace Nodalys {

// Source element handlers
[[nodiscard]] std::expected<void, std::string> AddSourceLine(const ElementLine &element);
[[nodiscard]] std::expected<void, std::string> AddBehavioralLine(const ElementLine &element);
[[nodiscard]] std::expected<void, std::string> AddVoltageControlledLine(const ElementLine &element);
[[nodiscard]] std::expected<void, std::string> AddCurrentControlledLine(const ElementLine &element);

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_ELEMENT_SOURCES_H
