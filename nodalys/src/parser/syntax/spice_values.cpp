/**
 * @file    spice_values.cpp
 * @brief   Evaluates the numbers of SPICE text: literals with scale suffixes and expressions of named parameters.
 */

#include "parser/syntax/spice_values.h"

#include "nodalys/expression.h"
#include "parser/syntax/spice_text.h"
#include "text/text_case.h"

#include <cctype>
#include <cstddef>
#include <format>

namespace Nodalys {

namespace {

/// Digits to print a double so that it reads back to the same value.
constexpr int ROUND_TRIP_DIGITS = 17;

bool IsDigit(const char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool IsWordStart(const char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
}

bool IsWordCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

/**
 * @brief   Tells whether a number starts at a position: a digit, or a point before one, as in ".5".
 */
bool IsNumberStart(const std::string_view text, const std::size_t position) {
    return IsDigit(text[position])
        || (text[position] == '.' && position + 1 < text.size() && IsDigit(text[position + 1]));
}

/**
 * @brief   The end of the number that starts at a position, with its scale suffix and a signed exponent.
 */
std::size_t FindNumberEnd(const std::string_view text, const std::size_t start) {
    std::size_t position = start;
    while (position < text.size()) {
        const char character = text[position];
        const bool is_exponent_sign = (character == '+' || character == '-') && position > start
                                   && (text[position - 1] == 'e' || text[position - 1] == 'E')
                                   && position + 1 < text.size() && IsDigit(text[position + 1]);
        if (!IsWordCharacter(character) && character != '.' && !is_exponent_sign) {
            break;
        }
        ++position;
    }
    return position;
}

/**
 * @brief   The end of the parenthesis that opens at a position, or the end of the text when it never closes.
 */
std::size_t FindClosingParenthesis(const std::string_view text, const std::size_t open) {
    int depth = 0;
    for (std::size_t position = open; position < text.size(); ++position) {
        if (text[position] == '(') {
            ++depth;
        } else if (text[position] == ')' && --depth == 0) {
            return position + 1;
        }
    }
    return text.size();
}

} // namespace

/**
 * @brief   Replaces the names of parameters in an expression by their values.
 * @details A name followed by a parenthesis is a function and stays; the references v(...) and i(...) are copied as
 *          they are, since the names inside them are nodes and sources, never parameters. Numbers such as "1e-3" and
 *          "4.7k" are skipped whole, so a scale suffix is never read as a name.
 * @param[in] text    Expression, braces and quotes already removed.
 * @param[in] lookup  Finds the value of a parameter by its upper-case name.
 * @return  The expression with each parameter written as a parenthesized number.
 */
std::string SubstituteParameters(const std::string_view text, const ParameterLookup &lookup) {
    std::string result;
    std::size_t position = 0;
    while (position < text.size()) {
        if (IsNumberStart(text, position)) {
            const std::size_t end = FindNumberEnd(text, position);
            result.append(text.substr(position, end - position));
            position = end;
            continue;
        }
        const std::optional<std::size_t> word_end = FindWordEnd(text, position);
        if (!word_end) {
            result.push_back(text[position]);
            ++position;
            continue;
        }
        const std::string_view word = text.substr(position, *word_end - position);
        if (const std::optional<std::size_t> call_end = FindCallEnd(text, *word_end, word)) {
            result.append(text.substr(position, *call_end - position));
            position = *call_end;
            continue;
        }
        if (const std::optional<double> value = lookup(ToUpper(word))) {
            result += std::format("({:.{}g})", *value, ROUND_TRIP_DIGITS);
        } else {
            result.append(word);
        }
        position = *word_end;
    }
    return result;
}

/**
 * @brief   Removes the braces or single quotes that wrap a value in SPICE text.
 */
std::string_view StripValueBrackets(std::string_view text) {
    text = TrimSpaces(text);
    const bool is_braced = text.size() >= 2 && text.front() == '{' && text.back() == '}';
    const bool is_quoted = text.size() >= 2 && text.front() == '\'' && text.back() == '\'';
    return is_braced || is_quoted ? TrimSpaces(text.substr(1, text.size() - 2)) : text;
}

/**
 * @brief   Evaluates a number as SPICE text writes it: a literal such as "4.7k", or an expression of parameters such
 *          as "{2 * RVAL + 1k}", with or without its braces.
 * @param[in] text    Value text.
 * @param[in] lookup  Finds the value of a parameter by its upper-case name.
 * @return  The value, or what was not understood.
 */
std::expected<double, std::string> EvaluateValue(const std::string_view text, const ParameterLookup &lookup) {
    // A literal goes through the expression reader too: ParseSpiceNumber() stops at the first character it does not
    // know, so it would read "2 * RVAL" as 2
    const std::string_view bare = StripValueBrackets(text);
    const auto expression = Expression::Parse(SubstituteParameters(bare, lookup));
    if (!expression) {
        return std::unexpected(std::format("the value '{}' is not understood: {}", bare, expression.error()));
    }
    if (!expression->GetInputs().empty() || expression->IsTimeDependent()) {
        return std::unexpected(std::format("the value '{}' reads a voltage, a current or the time, and a value of a "
                                           "parameter or an element must be constant",
                                           bare));
    }
    const auto value = expression->Evaluate({}, 0.0);
    if (!value) {
        return std::unexpected(std::format("the value '{}' cannot be evaluated: {}", bare, value.error()));
    }
    return value->Value;
}

/**
 * @brief   Finds the end of the name that starts at a position, such as a parameter or a function.
 * @param[in] text      Expression.
 * @param[in] position  Where the name may start.
 * @return  The position just after its last character, or none when no name starts there: a name starts with a letter
 *          or an underscore and goes on with letters, digits and underscores.
 */
std::optional<std::size_t> FindWordEnd(const std::string_view text, const std::size_t position) {
    if (position >= text.size() || !IsWordStart(text[position])) {
        return std::nullopt;
    }
    std::size_t end = position + 1;
    while (end < text.size() && IsWordCharacter(text[end])) {
        ++end;
    }
    return end;
}

/**
 * @brief   Finds how much of a call is copied as written, when a name is followed by a parenthesis.
 * @details A reference v(...) or i(...) is copied whole, since the names inside it are nodes and sources. A function
 *          keeps only its name, so the parameters among its arguments are still replaced.
 * @param[in] text      Expression.
 * @param[in] word_end  Position just after the name.
 * @param[in] word      The name.
 * @return  The position after the part copied as written: the closing parenthesis of a reference, or the end of the
 *          name of a function. None when no parenthesis follows the name, spaces aside, which makes it a parameter.
 */
std::optional<std::size_t>
FindCallEnd(const std::string_view text, const std::size_t word_end, const std::string_view word) {
    std::size_t after = word_end;
    while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after])) != 0) {
        ++after;
    }
    if (after >= text.size() || text[after] != '(') {
        return std::nullopt;
    }
    const std::string upper = ToUpper(word);
    const bool is_reference = upper == "V" || upper == "I";
    return is_reference ? FindClosingParenthesis(text, after) : word_end;
}

} // namespace Nodalys
