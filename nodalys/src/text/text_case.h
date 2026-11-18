/**
 * @file    text_case.h
 * @brief   Changes the case of ASCII text, as SPICE reads names, keys and words without regard to case.
 */

#ifndef NODALYS_TEXT_TEXT_CASE_H
#define NODALYS_TEXT_TEXT_CASE_H

#include <string>
#include <string_view>

namespace Nodalys {

// Case
std::string ToLower(std::string_view text);
std::string ToUpper(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_TEXT_TEXT_CASE_H
