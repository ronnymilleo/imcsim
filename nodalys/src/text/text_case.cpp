/**
 * @file    text_case.cpp
 * @brief   Changes the case of ASCII text and compares it without regard to case, as SPICE reads names, keys
 *          and words.
 */

#include "text/text_case.h"

#include <algorithm>
#include <cctype>

namespace Nodalys {

/**
 * @brief   Tells whether one name comes before another when case is ignored.
 * @param[in] left   Name in ASCII.
 * @param[in] right  Name in ASCII.
 * @return  True when the lowercase left comes before the lowercase right.
 */
bool CaseInsensitiveLess::operator()(const std::string_view left, const std::string_view right) const {
    return std::ranges::lexicographical_compare(left, right, [](const unsigned char first, const unsigned char second) {
        return std::tolower(first) < std::tolower(second);
    });
}

/**
 * @brief   Lowers the case of a text.
 * @param[in] text  Text in ASCII; other bytes stay as they are.
 * @return  The text in lowercase.
 */
std::string ToLower(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lower;
}

/**
 * @brief   Raises the case of a text.
 * @param[in] text  Text in ASCII; other bytes stay as they are.
 * @return  The text in uppercase.
 */
std::string ToUpper(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return upper;
}

} // namespace Nodalys
