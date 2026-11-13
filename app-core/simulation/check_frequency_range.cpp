/**
 * @file    check_frequency_range.cpp
 * @brief   Checks the frequency range that the AC sweep, the noise and the S-parameter analyses share.
 */

#include "simulation/check_frequency_range.h"

#include <format>
#include <optional>
#include <string>

namespace Core {

namespace {

/// Most points per decade a sweep may ask for, so a mistyped setting cannot run for minutes or exhaust memory.
constexpr int MAX_POINTS_PER_DECADE = 1000;

} // namespace

/**
 * @brief   Checks a logarithmic frequency range.
 * @param[in] start_frequency    First frequency, in hertz.
 * @param[in] stop_frequency     Last frequency, in hertz.
 * @param[in] points_per_decade  Points in each decade.
 * @return  What is wrong with the range, in words for the user, or no value when nothing is.
 */
std::optional<std::string> CheckFrequencyRange(const double start_frequency, const double stop_frequency,
                                               const int points_per_decade) {
    if (start_frequency <= 0.0) {
        return "The start frequency must be positive";
    }
    if (stop_frequency <= start_frequency) {
        return "The stop frequency must be higher than the start frequency";
    }
    if (points_per_decade < 1 || points_per_decade > MAX_POINTS_PER_DECADE) {
        return std::format("The points per decade must be between 1 and {}", MAX_POINTS_PER_DECADE);
    }
    return std::nullopt;
}

} // namespace Core
