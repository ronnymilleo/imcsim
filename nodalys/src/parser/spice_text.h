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

// Spaces
std::string_view TrimSpaces(std::string_view text);

// Names
bool IsGroundName(std::string_view upper_name);

// Lines and tokens
std::vector<std::string> SplitLogicalLines(std::string_view text);
std::vector<std::string> SplitTokens(std::string_view line);

} // namespace Nodalys

#endif // NODALYS_PARSER_SPICE_TEXT_H
