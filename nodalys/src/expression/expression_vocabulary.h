/**
 * @file    expression_vocabulary.h
 * @brief   Built-in functions, constants, refused SPICE names, and reference-argument helpers for expressions.
 */

#ifndef NODALYS_EXPRESSION_VOCABULARY_H
#define NODALYS_EXPRESSION_VOCABULARY_H

#include "expression/expression_program.h"
#include <optional>
#include <string>
#include <string_view>

namespace Nodalys {

/// Reason reported when an expression references `hertz`.
inline constexpr std::string_view HERTZ_REASON =
    "hertz, the frequency, is not supported by Nodalys in an expression: it would make the small-signal gain depend on "
    "the frequency, which an AC sweep cannot stamp once";

/**
 * @struct  FunctionEntry
 * @brief   A built-in function known to the expression language and its arity.
 */
struct FunctionEntry {
    std::string_view Name;
    Operation Kind;
    int OperandCount;
};

// Vocabulary lookups
[[nodiscard]] std::optional<double> LookupBuiltinConstant(std::string_view lower_name);
[[nodiscard]] std::optional<FunctionEntry> LookupExpressionFunction(std::string_view lower_name);
[[nodiscard]] std::optional<std::string> LookupRefusedFunctionReason(std::string_view written,
                                                                     std::string_view lower_name);

// Reference input helpers
[[nodiscard]] bool IsSameExpressionInput(const ExpressionInput &first, const ExpressionInput &second);
[[nodiscard]] std::optional<ExpressionInput> ReadReferenceInput(std::string_view arguments, bool is_voltage);

} // namespace Nodalys

#endif // NODALYS_EXPRESSION_VOCABULARY_H
