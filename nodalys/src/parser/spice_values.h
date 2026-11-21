/**
 * @file    spice_values.h
 * @brief   Evaluates the numbers of SPICE text: literals with scale suffixes and expressions of named parameters.
 */

#ifndef NODALYS_PARSER_SPICE_VALUES_H
#define NODALYS_PARSER_SPICE_VALUES_H

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Nodalys {

/// Finds the value of a parameter by its upper-case name, or none when no parameter has that name.
using ParameterLookup = std::function<std::optional<double>(const std::string &name)>;

// Values
std::string SubstituteParameters(std::string_view text, const ParameterLookup &lookup);
std::string_view StripValueBrackets(std::string_view text);
std::expected<double, std::string> EvaluateValue(std::string_view text, const ParameterLookup &lookup);

// Words
std::optional<std::size_t> FindWordEnd(std::string_view text, std::size_t position);
std::optional<std::size_t> FindCallEnd(std::string_view text, std::size_t word_end, std::string_view word);

} // namespace Nodalys

#endif // NODALYS_PARSER_SPICE_VALUES_H
