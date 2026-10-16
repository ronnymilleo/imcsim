/**
 * @file    standard_values.h
 * @brief   Standard (preferred) component values: the E series of IEC 60063.
 */

#ifndef IMCSIM_STANDARD_VALUES_H
#define IMCSIM_STANDARD_VALUES_H

#include <optional>
#include <string_view>

namespace Core {

/**
 * @enum    ValueSeries
 * @brief   The set of values a resistor, capacitor or inductor may take.
 * @details Each E series has that many values per decade, spaced about evenly on a log scale, so that a part
 *          of the series is within its tolerance of the next one: E6 is 20%, E12 10%, E24 5%, E48 2%, E96 1% and
 *          E192 0.5%. Free allows any value.
 */
enum class ValueSeries {
    Free,
    E6,
    E12,
    E24,
    E48,
    E96,
    E192,
};

// Names and tolerances
const char *GetSeriesName(ValueSeries series);
std::optional<ValueSeries> ParseSeriesName(std::string_view text);
double GetSeriesTolerancePercent(ValueSeries series);

// Standard values
double NearestStandardValue(ValueSeries series, double value);
double StepStandardValue(ValueSeries series, double value, int direction);

} // namespace Core

#endif // IMCSIM_STANDARD_VALUES_H
