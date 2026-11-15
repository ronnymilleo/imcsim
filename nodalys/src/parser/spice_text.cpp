/**
 * @file    spice_text.cpp
 * @brief   Splits SPICE text into logical lines and tokens, keeping braces, quotes and parentheses together.
 */

#include "parser/spice_text.h"

#include <cctype>
#include <cstddef>

namespace Nodalys {

namespace {

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

/**
 * @brief   Cuts a line at the first inline comment: a semicolon, or a dollar sign that starts a word.
 */
std::string_view StripInlineComment(const std::string_view line) {
    int depth = 0;
    bool is_quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '\'') {
            is_quoted = !is_quoted;
        } else if (!is_quoted && character == '{') {
            ++depth;
        } else if (!is_quoted && character == '}') {
            --depth;
        } else if (!is_quoted && depth <= 0 &&
                   (character == ';' || (character == '$' && (index == 0 || IsSpace(line[index - 1]))))) {
            return line.substr(0, index);
        }
    }
    return line;
}

} // namespace

/**
 * @brief   Upper-cases text, since SPICE reads names without regard to case.
 */
std::string ToUpperCase(const std::string_view text) {
    std::string upper;
    upper.reserve(text.size());
    for (const char character : text) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return upper;
}

/**
 * @brief   Removes the spaces at both ends of a text.
 */
std::string_view TrimSpaces(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/**
 * @brief   Splits text into the lines SPICE reads: continuation lines are joined to the one above, and blank lines,
 *          comment lines and inline comments are dropped.
 * @param[in] text  Text with one or more lines.
 * @return  The logical lines, trimmed.
 */
std::vector<std::string> SplitLogicalLines(const std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view raw = text.substr(start, end == std::string_view::npos ? end : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        const std::string_view line = TrimSpaces(StripInlineComment(TrimSpaces(raw)));
        if (line.empty() || TrimSpaces(raw).front() == '*') {
            continue;
        }
        if (line.front() == '+' && !lines.empty()) {
            lines.back().push_back(' ');
            lines.back().append(TrimSpaces(line.substr(1)));
        } else {
            lines.emplace_back(line);
        }
    }
    return lines;
}

/**
 * @brief   Splits a line into tokens at spaces and commas.
 * @details A token keeps together what sits inside parentheses, braces or single quotes, even with spaces in it, and
 *          spaces around an equals sign do not split, so "R = {2 * RVAL}" is the two tokens "R={2 * RVAL}" and none.
 * @param[in] line  Logical line.
 * @return  The tokens, as written.
 */
std::vector<std::string> SplitTokens(const std::string_view line) {
    std::vector<std::string> tokens;
    std::string token;
    int depth = 0;
    bool is_quoted = false;
    const auto flush = [&] {
        if (!token.empty()) {
            tokens.push_back(std::move(token));
            token.clear();
        }
    };
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '\'') {
            is_quoted = !is_quoted;
        } else if (!is_quoted && (character == '(' || character == '{')) {
            ++depth;
        } else if (!is_quoted && (character == ')' || character == '}')) {
            --depth;
        }
        const bool is_separator = (IsSpace(character) || character == ',') && depth <= 0 && !is_quoted;
        if (!is_separator) {
            token.push_back(character);
            continue;
        }
        // Spaces around an equals sign belong to the assignment
        std::size_t next = index;
        while (next < line.size() && (IsSpace(line[next]) || line[next] == ',')) {
            ++next;
        }
        const bool is_before_equals = next < line.size() && line[next] == '=' && !token.empty();
        const bool is_after_equals = !token.empty() && token.back() == '=';
        if (is_before_equals || is_after_equals) {
            index = next - 1;
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

} // namespace Nodalys
