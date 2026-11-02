/**
 * @file    standard_values.cpp
 * @brief   Standard (preferred) component values: the E series of IEC 60063.
 */

#include "standard_values.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <utility>
#include <vector>

namespace Core {

namespace {

// The IEC tables of the three oldest series were rounded by hand, so they are written out
constexpr auto E6_MANTISSAS = std::to_array<double>({1.0, 1.5, 2.2, 3.3, 4.7, 6.8});
constexpr auto E12_MANTISSAS = std::to_array<double>({1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2});
constexpr auto E24_MANTISSAS = std::to_array<double>({1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
                                                      3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1});

constexpr auto SERIES_NAMES = std::to_array<std::pair<ValueSeries, std::string_view>>({
    {ValueSeries::FREE, "Free"},
    {ValueSeries::E6, "E6"},
    {ValueSeries::E12, "E12"},
    {ValueSeries::E24, "E24"},
    {ValueSeries::E48, "E48"},
    {ValueSeries::E96, "E96"},
    {ValueSeries::E192, "E192"},
});

// The newer series are 10^(i/N) rounded to three digits
std::vector<double> ComputeMantissas(const int values_per_decade) {
    std::vector<double> mantissas;
    for (int index = 0; index < values_per_decade; ++index) {
        const double exact = std::pow(10.0, static_cast<double>(index) / values_per_decade);
        mantissas.push_back(std::round(exact * 100.0) / 100.0);
    }
    return mantissas;
}

// The mantissas of a series, from 1 up to below 10; empty for the free series
const std::vector<double> &GetMantissas(const ValueSeries series) {
    static const std::vector<double> e6(E6_MANTISSAS.begin(), E6_MANTISSAS.end());
    static const std::vector<double> e12(E12_MANTISSAS.begin(), E12_MANTISSAS.end());
    static const std::vector<double> e24(E24_MANTISSAS.begin(), E24_MANTISSAS.end());
    static const std::vector<double> e48 = ComputeMantissas(48);
    static const std::vector<double> e96 = ComputeMantissas(96);
    static const std::vector<double> e192 = ComputeMantissas(192);
    static const std::vector<double> free;
    switch (series) {
    case ValueSeries::E6:
        return e6;
    case ValueSeries::E12:
        return e12;
    case ValueSeries::E24:
        return e24;
    case ValueSeries::E48:
        return e48;
    case ValueSeries::E96:
        return e96;
    case ValueSeries::E192:
        return e192;
    case ValueSeries::FREE:
        break;
    }
    return free;
}

// A value of a series: the decade it is in (the power of ten) and which mantissa of the decade
struct Position {
    int Decade = 0;
    std::size_t Index = 0;
};

// Goes through the decimal text so that 4.7 in the decade 3 is exactly the double 4700, which 4.7 * 1000.0 is not
double ValueAt(const std::vector<double> &mantissas, const Position position) {
    const std::string text = std::format("{}e{}", mantissas[position.Index], position.Decade);
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// The value of the series closest to the given one on a log scale, which is how the series is spaced
Position FindNearest(const std::vector<double> &mantissas, const double value) {
    const int decade = static_cast<int>(std::floor(std::log10(value)));
    Position best;
    double best_distance = std::numeric_limits<double>::infinity();
    // The nearest may be in the decade below or above, when the value is close to a power of ten
    for (int candidate_decade = decade - 1; candidate_decade <= decade + 1; ++candidate_decade) {
        for (std::size_t index = 0; index < mantissas.size(); ++index) {
            const Position candidate{candidate_decade, index};
            const double distance = std::abs(std::log(value / ValueAt(mantissas, candidate)));
            if (distance < best_distance) {
                best_distance = distance;
                best = candidate;
            }
        }
    }
    return best;
}

// One value up or down the series, into the next decade when it runs past the end of one
Position Shift(const std::vector<double> &mantissas, Position position, const int direction) {
    if (direction > 0) {
        if (++position.Index == mantissas.size()) {
            position.Index = 0;
            ++position.Decade;
        }
    } else if (position.Index == 0) {
        position.Index = mantissas.size() - 1;
        --position.Decade;
    } else {
        --position.Index;
    }
    return position;
}

bool IsUsable(const double value) {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

/**
 * @brief   Returns the name of a series, as the preferences file keeps it.
 * @param[in] series  Any series.
 * @return  "Free", "E6", "E12", "E24", "E48", "E96" or "E192".
 */
const char *GetSeriesName(const ValueSeries series) {
    const auto found = std::ranges::find(SERIES_NAMES, series, &std::pair<ValueSeries, std::string_view>::first);
    return found->second.data();
}

/**
 * @brief   Reads a series from its name.
 * @param[in] text  A name GetSeriesName() gives.
 * @return  The series, or no value for text that is not a name.
 */
std::optional<ValueSeries> ParseSeriesName(const std::string_view text) {
    const auto found = std::ranges::find(SERIES_NAMES, text, &std::pair<ValueSeries, std::string_view>::second);
    if (found == SERIES_NAMES.end()) {
        return std::nullopt;
    }
    return found->first;
}

/**
 * @brief   Returns how far a part of a series may be from its nominal value.
 * @param[in] series  Any series.
 * @return  The tolerance in percent: 20, 10, 5, 2, 1 or 0.5 for E6 to E192, and 0 for the free series.
 */
double GetSeriesTolerancePercent(const ValueSeries series) {
    switch (series) {
    case ValueSeries::E6:
        return 20.0;
    case ValueSeries::E12:
        return 10.0;
    case ValueSeries::E24:
        return 5.0;
    case ValueSeries::E48:
        return 2.0;
    case ValueSeries::E96:
        return 1.0;
    case ValueSeries::E192:
        return 0.5;
    case ValueSeries::FREE:
        break;
    }
    return 0.0;
}

/**
 * @brief   Finds the standard value closest to a value.
 * @param[in] series  Series to take the value from.
 * @param[in] value   Any positive value, in any decade.
 * @return  The closest value of the series, measured on a log scale; the value itself for the free series or when
 *          it is not a positive number.
 */
double NearestStandardValue(const ValueSeries series, const double value) {
    const std::vector<double> &mantissas = GetMantissas(series);
    if (mantissas.empty() || !IsUsable(value)) {
        return value;
    }
    return ValueAt(mantissas, FindNearest(mantissas, value));
}

/**
 * @brief   Finds the next standard value above or below a value.
 * @param[in] series     Series to take the value from.
 * @param[in] value      Any positive value, on the series or not.
 * @param[in] direction  Positive for the next value up, negative for the next down.
 * @return  The next value of the series strictly past the given one in that direction, going on into the next
 *          decade; the value itself for the free series or when it is not a positive number.
 */
double StepStandardValue(const ValueSeries series, const double value, const int direction) {
    const std::vector<double> &mantissas = GetMantissas(series);
    if (mantissas.empty() || !IsUsable(value) || direction == 0) {
        return value;
    }
    Position position = FindNearest(mantissas, value);
    const double nearest = ValueAt(mantissas, position);
    // The nearest value may already be on the right side of a value that is off the series
    constexpr double SAME_VALUE_TOLERANCE = 1e-9;
    const bool on_series = std::abs(nearest - value) <= SAME_VALUE_TOLERANCE * value;
    const bool past = direction > 0 ? nearest > value : nearest < value;
    if (on_series || !past) {
        position = Shift(mantissas, position, direction);
    }
    return ValueAt(mantissas, position);
}

} // namespace Core
