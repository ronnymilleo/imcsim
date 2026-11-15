/**
 * @file    spice_values.h
 * @brief   Evaluates the numbers of SPICE text: literals with scale suffixes and expressions of named parameters.
 */

#ifndef NODALYS_PARSER_SPICE_VALUES_H
#define NODALYS_PARSER_SPICE_VALUES_H

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
std::expected<double, std::string> EvaluateValue(std::string_view text, const ParameterLookup &lookup);
std::string_view StripValueBrackets(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_PARSER_SPICE_VALUES_H
