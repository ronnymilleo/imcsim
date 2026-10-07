/**
 * @file    spice_value.cpp
 * @brief   Reads and writes component values with scale suffixes, such as "4.7k" or "100n".
 */

#include "spice_value.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace Core {

namespace {

/**
 * @struct  ScaleSuffix
 * @brief   A scale suffix as the user types it, the same suffix as SPICE reads it, and its factor.
 * @details The user side is case sensitive (M is mega, m is milli). SPICE ignores case and reads M as milli,
 *          so mega must be written "Meg" in netlists.
 */
struct ScaleSuffix {
    std::string_view Text;
    std::string_view SpiceText;
    double Factor;
};

// Ordered from largest to smallest so formatting can pick the first one that fits
constexpr auto Suffixes = std::to_array<ScaleSuffix>({
    {"T", "T", 1e12},
    {"G", "G", 1e9},
    {"M", "Meg", 1e6},
    {"k", "k", 1e3},
    {"", "", 1.0},
    {"m", "m", 1e-3},
    {"u", "u", 1e-6},
    {"n", "n", 1e-9},
    {"p", "p", 1e-12},
    {"f", "f", 1e-15},
});

// The micro sign, as typed on many keyboards, encoded in UTF-8
constexpr std::string_view MicroSign = "\xC2\xB5";

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// Such a value would read like "9.9e-05f" with the smallest suffix, so it is written in exponent notation instead
bool IsBelowSuffixes(const double value) {
    return std::abs(value) < Suffixes.back().Factor;
}

const ScaleSuffix &ChooseSuffix(const double value) {
    const double magnitude = std::abs(value);
    for (const ScaleSuffix &suffix : Suffixes) {
        if (magnitude >= suffix.Factor) {
            return suffix;
        }
    }
    return Suffixes.back();
}

} // namespace

/**
 * @brief   Parses a value typed by the user.
 * @param[in] text  A number, optionally followed by a case-sensitive scale suffix (T, G, M, k, m, u or the
 *                  micro sign, n, p, f) and then optionally by the unit, such as "4.7k", "1M" or "100nF".
 * @param[in] unit  The only unit accepted after the suffix, such as "Ohm"; anything else makes the text invalid,
 *                  so a wrong-case suffix like "10K" is rejected instead of silently read as 10.
 * @return  The value, or no value when the text is not valid.
 */
std::optional<double> ParseValue(const std::string_view text, const std::string_view unit) {
    const std::string_view trimmed = Trim(text);
    double number = 0.0;
    const auto [rest_begin, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
    if (error != std::errc{} || !std::isfinite(number)) {
        return std::nullopt;
    }

    std::string_view rest(rest_begin, trimmed.data() + trimmed.size() - rest_begin);
    double factor = 1.0;
    if (rest.starts_with(MicroSign)) {
        factor = 1e-6;
        rest.remove_prefix(MicroSign.size());
    } else {
        for (const ScaleSuffix &suffix : Suffixes) {
            // Every text starts with the empty suffix, which stands for no scaling
            if (!suffix.Text.empty() && rest.starts_with(suffix.Text)) {
                factor = suffix.Factor;
                rest.remove_prefix(suffix.Text.size());
                break;
            }
        }
    }

    if (!rest.empty() && rest != unit) {
        return std::nullopt;
    }
    return number * factor;
}

/**
 * @brief   Formats a value for the user, with the suffix that keeps the number between 1 and 1000.
 * @param[in] value  Value to format.
 * @return  Text such as "4.7k", "1M" or "100n", with up to four significant digits, or exponent notation such as
 *          "9.9e-20" below the femto range. ParseValue() reads it back.
 */
std::string FormatValue(const double value) {
    if (value == 0.0) {
        return "0";
    }
    if (IsBelowSuffixes(value)) {
        return std::format("{:.4g}", value);
    }
    const ScaleSuffix &suffix = ChooseSuffix(value);
    return std::format("{:.4g}{}", value / suffix.Factor, suffix.Text);
}

/**
 * @brief   Formats a value for a SPICE netlist.
 * @param[in] value  Value to format.
 * @return  Like FormatValue(), but mega is written "Meg" because SPICE reads "M" as milli.
 */
std::string FormatSpiceValue(const double value) {
    if (value == 0.0) {
        return "0";
    }
    if (IsBelowSuffixes(value)) {
        return std::format("{:.4g}", value);
    }
    const ScaleSuffix &suffix = ChooseSuffix(value);
    return std::format("{:.4g}{}", value / suffix.Factor, suffix.SpiceText);
}

} // namespace Core
