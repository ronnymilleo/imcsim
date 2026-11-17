/**
 * @file    spice_text.h
 * @brief   Splits SPICE text into logical lines and tokens, keeping braces, quotes and parentheses together.
 */

#ifndef NODALYS_PARSER_SPICE_TEXT_H
#define NODALYS_PARSER_SPICE_TEXT_H

#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/// The word that may start the parameters of a .subckt or an X line, as in "params: gain=2".
constexpr std::string_view PARAMETER_LIST_MARK = "PARAMS:";

// Case and spaces
std::string ToUpperCase(std::string_view text);
std::string_view TrimSpaces(std::string_view text);

// Lines and tokens
std::vector<std::string> SplitLogicalLines(std::string_view text);
std::vector<std::string> SplitTokens(std::string_view line);
bool IsParameterStart(const std::string &token);

} // namespace Nodalys

#endif // NODALYS_PARSER_SPICE_TEXT_H
