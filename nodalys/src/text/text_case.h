/**
 * @file    text_case.h
 * @brief   Changes the case of ASCII text and compares it without regard to case, as SPICE reads names, keys
 *          and words.
 */

#ifndef NODALYS_TEXT_TEXT_CASE_H
#define NODALYS_TEXT_TEXT_CASE_H

#include <string>
#include <string_view>

namespace Nodalys {

/**
 * @struct  CaseInsensitiveLess
 * @brief   Orders names as SPICE compares them, without regard to case, so a map keyed by it finds "Vin" as "VIN".
 * @details It is transparent, so a lookup can take a std::string_view without building a std::string.
 */
struct CaseInsensitiveLess {
    // NOLINTNEXTLINE(readability-identifier-naming): the standard library looks for this exact name
    using is_transparent = void;

    bool operator()(std::string_view left, std::string_view right) const;
};

// Case
std::string ToLower(std::string_view text);
std::string ToUpper(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_TEXT_TEXT_CASE_H
