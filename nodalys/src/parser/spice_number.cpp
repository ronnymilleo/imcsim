/**
 * @file    spice_number.cpp
 * @brief   Parses SPICE numeric literals with engineering scale suffixes.
 */

#include "parser/spice_number.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>

namespace Nodalys {

namespace {

struct Scale {
    std::string_view Suffix;
    double Factor;
};

// Ordered so longer prefixes are checked before shorter substrings (e.g. "meg" before "m", "mil" before "m")
constexpr auto SCALES = std::to_array<Scale>({
    {"meg", 1e6},
    {"mil", 25.4e-6},
    {"k", 1e3},
    {"m", 1e-3},
    {"u", 1e-6},
    {"n", 1e-9},
    {"p", 1e-12},
    {"f", 1e-15},
    {"g", 1e9},
    {"t", 1e12},
});

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (const char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::optional<double> ParseSpiceNumber(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        return std::nullopt;
    }

    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || !std::isfinite(value)) {
        return std::nullopt;
    }

    std::string_view rest = text.substr(static_cast<std::size_t>(end - text.data()));
    if (rest.empty()) {
        return value;
    }

    // Handle UTF-8 micro sign: "\xC2\xB5"
    if (rest.starts_with("\xC2\xB5")) {
        return value * 1e-6;
    }

    const std::string lower = ToLower(rest);
    for (const auto &scale : SCALES) {
        if (lower.starts_with(scale.Suffix)) {
            return value * scale.Factor;
        }
    }

    return value;
}

} // namespace Nodalys
