/**
 * @file    text_case.cpp
 * @brief   Changes the case of ASCII text, as SPICE reads names, keys and words without regard to case.
 */

#include "text/text_case.h"

#include <algorithm>
#include <cctype>

namespace Nodalys {

/**
 * @brief   Lowers the case of a text.
 * @param[in] text  Text in ASCII; other bytes stay as they are.
 * @return  The text in lowercase.
 */
std::string ToLower(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

/**
 * @brief   Raises the case of a text.
 * @param[in] text  Text in ASCII; other bytes stay as they are.
 * @return  The text in uppercase.
 */
std::string ToUpper(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
    return upper;
}

} // namespace Nodalys
