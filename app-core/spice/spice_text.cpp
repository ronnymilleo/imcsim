/**
 * @file    spice_text.cpp
 * @brief   Splits and compares the text of SPICE files, which ignore case and may have DOS line ends.
 */

#include "spice/spice_text.h"

#include <algorithm>
#include <cctype>

namespace Core {

namespace {

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

} // namespace

/**
 * @brief   Lowers the case of a text, as SPICE compares names and directives.
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
 * @brief   Drops the spaces, tabs and line ends around a text.
 * @param[in] text  Text to trim.
 * @return  A view of the text without them, empty when the text is all spaces.
 */
std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/**
 * @brief   Splits a file into its lines.
 * @param[in] text  Contents of the file, with Unix or DOS line ends.
 * @return  Every line without its line end, so line N of the file is at index N - 1.
 */
std::vector<std::string> SplitLines(const std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.emplace_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

/**
 * @brief   Splits a line into the words between its spaces and tabs.
 * @param[in] text  A line.
 * @return  Its words in order; none for a blank line.
 */
std::vector<std::string> SplitWords(const std::string_view text) {
    std::vector<std::string> words;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && IsSpace(text[position])) {
            ++position;
        }
        const std::size_t start = position;
        while (position < text.size() && !IsSpace(text[position])) {
            ++position;
        }
        if (position > start) {
            words.emplace_back(text.substr(start, position - start));
        }
    }
    return words;
}

} // namespace Core
