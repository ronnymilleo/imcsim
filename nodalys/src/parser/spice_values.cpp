/**
 * @file    spice_values.cpp
 * @brief   Evaluates the numbers of SPICE text: literals with scale suffixes and expressions of named parameters.
 */

#include "parser/spice_values.h"

#include "nodalys/expression.h"
#include "parser/spice_text.h"

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

bool IsWordCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

/**
 * @brief   The end of the number that starts at a position, with its scale suffix and a signed exponent.
 */
std::size_t FindNumberEnd(const std::string_view text, const std::size_t start) {
    std::size_t position = start;
    while (position < text.size()) {
        const char character = text[position];
        const bool is_exponent_sign = (character == '+' || character == '-') && position > start &&
                                      (text[position - 1] == 'e' || text[position - 1] == 'E') &&
                                      position + 1 < text.size() && IsDigit(text[position + 1]);
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
        const char character = text[position];
        const bool is_number_start =
            IsDigit(character) || (character == '.' && position + 1 < text.size() && IsDigit(text[position + 1]));
        if (is_number_start) {
            const std::size_t end = FindNumberEnd(text, position);
            result.append(text.substr(position, end - position));
            position = end;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(character)) && character != '_') {
            result.push_back(character);
            ++position;
            continue;
        }
        std::size_t end = position;
        while (end < text.size() && IsWordCharacter(text[end])) {
            ++end;
        }
        const std::string_view word = text.substr(position, end - position);
        std::size_t after = end;
        while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after])) != 0) {
            ++after;
        }
        if (after < text.size() && text[after] == '(') {
            const std::string upper = ToUpperCase(word);
            const bool is_reference = upper == "V" || upper == "I";
            const std::size_t close = is_reference ? FindClosingParenthesis(text, after) : end;
            result.append(text.substr(position, close - position));
            position = close;
            continue;
        }
        if (const std::optional<double> value = lookup(ToUpperCase(word))) {
            result += std::format("({:.{}g})", *value, ROUND_TRIP_DIGITS);
        } else {
            result.append(word);
        }
        position = end;
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

} // namespace Nodalys
