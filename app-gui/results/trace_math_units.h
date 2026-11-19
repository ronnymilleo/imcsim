/**
 * @file    trace_math_units.h
 * @brief   The units of math channels: powers of volts, amperes and seconds, how operations combine them and how they
 *          are written.
 */

#ifndef IMCSIM_TRACE_MATH_UNITS_H
#define IMCSIM_TRACE_MATH_UNITS_H

#include <string>

namespace GUI {

/**
 * @struct  Dimension
 * @brief   Physical unit of a value, as powers of volts, amperes and seconds, so V*I reads as watts.
 * @details Adding values of different units gives no known unit; such a result is still plotted, without one.
 */
struct Dimension {
    int Volts = 0;
    int Amperes = 0;
    int Seconds = 0;
    bool Known = true;

    bool operator==(const Dimension &) const = default;
};

// Combining
Dimension MultiplyUnits(Dimension first, Dimension second);
Dimension DivideUnits(Dimension first, Dimension second);
Dimension AddUnits(Dimension first, bool first_is_number, Dimension second, bool second_is_number);
Dimension GetSquareRootUnit(Dimension unit);

// Naming
std::string FormatUnit(Dimension unit);

} // namespace GUI

#endif // IMCSIM_TRACE_MATH_UNITS_H
