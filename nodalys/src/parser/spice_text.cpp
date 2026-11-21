/**
 * @file    spice_text.cpp
 * @brief   Splits SPICE text into logical lines and tokens, keeping braces, quotes and parentheses together.
 */

#include "parser/spice_text.h"

#include "text/text_case.h"

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
        } else if (!is_quoted && depth <= 0
                   && (character == ';' || (character == '$' && (index == 0 || IsSpace(line[index - 1]))))) {
            return line.substr(0, index);
        }
    }
    return line;
}

bool IsSeparator(const char character) {
    return IsSpace(character) || character == ',';
}

/**
 * @brief   Follows the parentheses, braces and single quotes of a line, inside which a separator splits nothing.
 */
class Grouping {
public:
    void Follow(const char character) {
        if (character == '\'') {
            m_IsQuoted = !m_IsQuoted;
        } else if (m_IsQuoted) {
            return;
        } else if (character == '(' || character == '{') {
            ++m_Depth;
        } else if (character == ')' || character == '}') {
            --m_Depth;
        }
    }

    bool IsOpen() const { return m_IsQuoted || m_Depth > 0; }

private:
    int m_Depth = 0;
    bool m_IsQuoted = false;
};

std::size_t SkipSeparators(const std::string_view line, std::size_t index) {
    while (index < line.size() && IsSeparator(line[index])) {
        ++index;
    }
    return index;
}

/**
 * @brief   Whether the separators before next sit inside an assignment, as in "R = 1k", so they split nothing.
 * @param[in] line   The line.
 * @param[in] next   Index of the first character after the separators.
 * @param[in] token  The token so far, before the separators.
 */
bool IsAssignmentGap(const std::string_view line, const std::size_t next, const std::string &token) {
    const bool is_before_equals = next < line.size() && line[next] == '=';
    return !token.empty() && (is_before_equals || token.ends_with('='));
}

void FlushToken(std::string &token, std::vector<std::string> &tokens) {
    if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
    }
}

} // namespace

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
 * @brief   Tells whether a node name is ground, as ngspice reads it: 0, or GND, which it takes for 0.
 * @param[in] upper_name  Node name in upper case; an empty name is ground too.
 */
bool IsGroundName(const std::string_view upper_name) {
    return upper_name.empty() || upper_name == "0" || upper_name == "GND";
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
    Grouping grouping;
    std::size_t index = 0;
    while (index < line.size()) {
        const char character = line[index];
        grouping.Follow(character);
        if (!IsSeparator(character) || grouping.IsOpen()) {
            token.push_back(character);
            ++index;
            continue;
        }
        const std::size_t next = SkipSeparators(line, index);
        if (!IsAssignmentGap(line, next, token)) {
            FlushToken(token, tokens);
        }
        index = next;
    }
    FlushToken(token, tokens);
    return tokens;
}

/**
 * @brief   Tells whether a token starts the parameters of a .subckt or an X line: the PARAMS: word or a NAME=VALUE.
 * @param[in] token  Token of the line, after its nodes.
 */
bool IsParameterStart(const std::string &token) {
    return ToUpper(token).starts_with(PARAMETER_LIST_MARK)
        || (token.find('=') != std::string::npos && token.front() != '{' && token.front() != '\'');
}

} // namespace Nodalys
