/**
 * @file    plot_ticks.cpp
 * @brief   Chooses the tick positions of a plot axis and writes their labels.
 */

#include "plots/plot_ticks.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

/**
 * @brief   Finds round tick positions (1, 2 or 5 times a power of ten) across a range.
 * @param[in] from          Lower end of the range.
 * @param[in] to            Upper end; must be above @p from.
 * @param[in] target_count  About how many intervals are wanted; at least 1.
 * @return  Ticks inside the range in ascending order, at most 64; empty for an invalid range or count.
 */
std::vector<double> FindNiceTicks(const double from, const double to, const int target_count) {
    std::vector<double> ticks;
    if (!(to > from) || !std::isfinite(from) || !std::isfinite(to) || target_count < 1) {
        return ticks;
    }
    const double raw_step = (to - from) / target_count;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
    const double normalized = raw_step / magnitude;
    const double step = (normalized <= 1.5   ? 1.0
                         : normalized <= 3.0 ? 2.0
                         : normalized <= 7.0 ? 5.0
                                             : 10.0) *
                        magnitude;
    const double first = std::ceil(from / step - 1e-9);
    for (double index = first; ticks.size() < 64; index += 1.0) {
        const double tick = index * step;
        if (tick > to + step * 1e-9) {
            break;
        }
        // Rounding leaves residues such as 1e-17 where the tick is zero
        ticks.push_back(std::abs(tick) < step * 1e-9 ? 0.0 : tick);
    }
    return ticks;
}

/**
 * @brief   Finds the powers of ten across a range, for a logarithmic axis.
 * @param[in] from      Lower end of the range; must be positive.
 * @param[in] to        Upper end; must be above @p from.
 * @param[in] max_count Most ticks wanted; when there are more decades, every second, third... decade is kept.
 * @return  Powers of ten inside the range in ascending order; empty for an invalid range.
 */
std::vector<double> FindDecadeTicks(const double from, const double to, const int max_count) {
    std::vector<double> ticks;
    if (!(from > 0.0) || !(to > from) || !std::isfinite(to) || max_count < 1) {
        return ticks;
    }
    const auto first = static_cast<int>(std::ceil(std::log10(from) - 1e-9));
    const auto last = static_cast<int>(std::floor(std::log10(to) + 1e-9));
    const int stride = std::max(1, (last - first + max_count) / max_count);
    for (int exponent = first; exponent <= last; exponent += stride) {
        ticks.push_back(std::pow(10.0, exponent));
    }
    return ticks;
}

/**
 * @brief   Formats a tick label for an axis whose ticks are a given step apart.
 * @param[in] value Tick value.
 * @param[in] step  Distance between ticks; sets how many decimals are shown.
 * @return  "0" for values that round to zero, never "-0"; exponent notation such as "2e-5" below 1e-3 or from 1e6
 *          in magnitude; otherwise the fewest decimals that show the step exactly.
 */
std::string FormatTick(const double value, const double step) {
    const double magnitude = std::abs(value);
    if (magnitude == 0.0 || magnitude < std::abs(step) * 1e-6) {
        return "0";
    }
    if (magnitude >= 1e6 || magnitude < 1e-3) {
        const auto value_exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        const auto step_exponent = static_cast<int>(std::floor(std::log10(std::abs(step)) + 1e-9));
        const std::string text = std::format("{:.{}e}", value, std::clamp(value_exponent - step_exponent, 0, 6));
        const std::size_t exponent_position = text.find('e');
        return std::format("{}e{}", text.substr(0, exponent_position), std::stoi(text.substr(exponent_position + 1)));
    }
    const double step_magnitude = std::abs(step);
    int decimals = 0;
    while (decimals < 8) {
        const double scaled = step_magnitude * std::pow(10.0, decimals);
        if (std::abs(scaled - std::round(scaled)) <= 1e-6 * scaled) {
            break;
        }
        ++decimals;
    }
    const std::string text = std::format("{:.{}f}", value, decimals);
    return text.find_first_not_of("-0.") == std::string::npos ? "0" : text;
}

} // namespace GUI
