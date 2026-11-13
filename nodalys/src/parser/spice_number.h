/**
 * @file    spice_number.h
 * @brief   Parses SPICE numeric literals with engineering scale suffixes.
 */

#ifndef NODALYS_PARSER_SPICE_NUMBER_H
#define NODALYS_PARSER_SPICE_NUMBER_H

#include <optional>
#include <string_view>

namespace Nodalys {

// Parsing
[[nodiscard]] std::optional<double> ParseSpiceNumber(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_PARSER_SPICE_NUMBER_H
