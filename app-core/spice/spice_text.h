/**
 * @file    spice_text.h
 * @brief   Splits and compares the text of SPICE files, which ignore case and may have DOS line ends.
 */

#ifndef IMCSIM_SPICE_TEXT_H
#define IMCSIM_SPICE_TEXT_H

#include <string>
#include <string_view>
#include <vector>

namespace Core {

// Text
std::string ToLower(std::string_view text);
std::string_view Trim(std::string_view text);
std::vector<std::string> SplitLines(std::string_view text);
std::vector<std::string> SplitWords(std::string_view text);

} // namespace Core

#endif // IMCSIM_SPICE_TEXT_H
