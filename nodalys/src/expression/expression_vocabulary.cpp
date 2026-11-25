/**
 * @file    expression_vocabulary.cpp
 * @brief   Built-in functions, constants, refused SPICE names, and reference-argument helpers for expressions.
 */

#include "expression/expression_vocabulary.h"

#include "text/text_case.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace Nodalys {

namespace {

constexpr double NOMINAL_E = 2.718281828459045;
constexpr double NOMINAL_PI = 3.141592653589793;
constexpr std::string_view RANDOM_REASON = "draws random numbers, which Nodalys does not run in an expression";

constexpr auto FUNCTIONS = std::to_array<FunctionEntry>({
    {"abs", Operation::ABS, 1},
    {"sqrt", Operation::SQRT, 1},
    {"exp", Operation::EXP, 1},
    // ngspice reads log() as the natural logarithm, like ln(), and log10() as the decimal one
    {"ln", Operation::LN, 1},
    {"log", Operation::LN, 1},
    {"log10", Operation::LOG10, 1},
    {"sin", Operation::SIN, 1},
    {"cos", Operation::COS, 1},
    {"tan", Operation::TAN, 1},
    {"asin", Operation::ASIN, 1},
    {"acos", Operation::ACOS, 1},
    {"atan", Operation::ATAN, 1},
    {"sinh", Operation::SINH, 1},
    {"cosh", Operation::COSH, 1},
    {"tanh", Operation::TANH, 1},
    {"asinh", Operation::ASINH, 1},
    {"acosh", Operation::ACOSH, 1},
    {"atanh", Operation::ATANH, 1},
    {"sgn", Operation::SIGN, 1},
    {"u", Operation::STEP, 1},
    {"u2", Operation::RAMP_STEP, 1},
    {"uramp", Operation::RAMP, 1},
    {"floor", Operation::FLOOR, 1},
    {"ceil", Operation::CEIL, 1},
    {"nint", Operation::NEAREST_INTEGER, 1},
    {"min", Operation::MIN, 2},
    {"max", Operation::MAX, 2},
    {"pow", Operation::POW, 2},
    {"pwr", Operation::PWR, 2},
});

constexpr auto REFUSED_FUNCTIONS = std::to_array<std::pair<std::string_view, std::string_view>>({
    {"limit",
     "limit() is a Monte Carlo function of ngspice (the nominal value plus or minus a deviation, chosen at "
     "random), not a clamp; clamp with min(max(x, low), high)"},
    {"atan2", "atan2() is not available in a behavioral expression of ngspice either; use atan(y / x)"},
    {"gauss", RANDOM_REASON},
    {"agauss", RANDOM_REASON},
    {"unif", RANDOM_REASON},
    {"aunif", RANDOM_REASON},
    {"rnd", RANDOM_REASON},
    {"random", RANDOM_REASON},
    {"exponential", RANDOM_REASON},
});

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool IsBareArgumentName(const std::string_view name) {
    return !name.empty() && std::ranges::none_of(name, IsSpace) && !name.contains(',');
}

} // namespace

/**
 * @brief   Looks up a named mathematical constant (`pi` or `e`).
 * @param[in] lower_name  Lowercase identifier name.
 * @return  Constant value when matched, or `std::nullopt`.
 */
std::optional<double> LookupBuiltinConstant(const std::string_view lower_name) {
    if (lower_name == "pi") {
        return NOMINAL_PI;
    }
    if (lower_name == "e") {
        return NOMINAL_E;
    }
    return std::nullopt;
}

/**
 * @brief   Looks up a supported built-in function by its lowercase name.
 * @param[in] lower_name  Lowercase function name.
 * @return  Matching FunctionEntry when supported, or `std::nullopt`.
 */
std::optional<FunctionEntry> LookupExpressionFunction(const std::string_view lower_name) {
    const auto found =
        std::ranges::find_if(FUNCTIONS, [lower_name](const FunctionEntry &entry) { return entry.Name == lower_name; });
    if (found == FUNCTIONS.end()) {
        return std::nullopt;
    }
    return *found;
}

/**
 * @brief   Checks whether a function name is a known ngspice function intentionally refused by Nodalys.
 * @param[in] written     Function name as written in the expression text.
 * @param[in] lower_name  Lowercase function name.
 * @return  User-facing refusal message when matched, or `std::nullopt`.
 */
std::optional<std::string> LookupRefusedFunctionReason(const std::string_view written,
                                                       const std::string_view lower_name) {
    const auto refused =
        std::ranges::find_if(REFUSED_FUNCTIONS, [lower_name](const auto &entry) { return entry.first == lower_name; });
    if (refused == REFUSED_FUNCTIONS.end()) {
        return std::nullopt;
    }
    const bool is_random = refused->second == RANDOM_REASON;
    return is_random ? std::format("{}() {}", written, RANDOM_REASON) : std::string(refused->second);
}

/**
 * @brief   Compares two expression inputs case-insensitively.
 * @param[in] first   First expression input descriptor.
 * @param[in] second  Second expression input descriptor.
 * @return  True when both inputs refer to the same voltage or current quantity.
 */
bool IsSameExpressionInput(const ExpressionInput &first, const ExpressionInput &second) {
    return first.Type == second.Type && ToLower(first.First) == ToLower(second.First)
        && ToLower(first.Second) == ToLower(second.Second);
}

/**
 * @brief   Parses the inside of `v(...)` or `i(...)` into an ExpressionInput descriptor.
 * @param[in] arguments   Substring between `(` and `)`.
 * @param[in] is_voltage  True for `v(...)`, false for `i(...)`.
 * @return  Parsed ExpressionInput, or `std::nullopt` when the argument list is malformed.
 */
std::optional<ExpressionInput> ReadReferenceInput(const std::string_view arguments, const bool is_voltage) {
    const std::size_t comma = arguments.find(',');
    const std::string_view first = Trim(arguments.substr(0, comma));
    if (!IsBareArgumentName(first)) {
        return std::nullopt;
    }
    ExpressionInput input{.Type = is_voltage ? ExpressionInput::Kind::VOLTAGE : ExpressionInput::Kind::CURRENT,
                          .First = std::string(first)};
    if (comma != std::string_view::npos) {
        const std::string_view second = Trim(arguments.substr(comma + 1));
        if (!is_voltage || !IsBareArgumentName(second)) {
            return std::nullopt;
        }
        input.Second = std::string(second);
    }
    return input;
}

} // namespace Nodalys
