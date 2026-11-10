#ifndef NODALYS_PARSER_SPICE_NUMBER_H
#define NODALYS_PARSER_SPICE_NUMBER_H

#include <optional>
#include <string_view>

namespace nodalys {

/**
 * @brief Parses a SPICE numeric literal with engineering suffixes (e.g. "4.7k", "10Meg", "100nF", "1e-3").
 * @param text The input string to parse.
 * @return The parsed double value, or std::nullopt if the text is not a valid number.
 */
std::optional<double> ParseSpiceNumber(std::string_view text);

} // namespace nodalys

#endif // NODALYS_PARSER_SPICE_NUMBER_H
