/**
 * @file    trace_math_units.cpp
 * @brief   The units of math channels: powers of volts, amperes and seconds, how operations combine them and how they
 *          are written.
 */

#include "results/trace_math_units.h"

#include <array>
#include <cstdlib>
#include <format>
#include <utility>

namespace GUI {

namespace {

/**
 * @brief   Adds the powers of the second unit, times the sign, to those of the first: 1 multiplies, -1 divides.
 */
Dimension Combine(const Dimension first, const Dimension second, const int sign) {
    return {.Volts = first.Volts + sign * second.Volts,
            .Amperes = first.Amperes + sign * second.Amperes,
            .Seconds = first.Seconds + sign * second.Seconds,
            .Known = first.Known && second.Known};
}

} // namespace

/**
 * @brief   Gives the unit of a product, such as volts times amperes.
 * @param[in] first   Unit of the first factor.
 * @param[in] second  Unit of the second factor.
 * @return  The unit with the powers of both added; unknown when either is.
 */
Dimension MultiplyUnits(const Dimension first, const Dimension second) {
    return Combine(first, second, 1);
}

/**
 * @brief   Gives the unit of a quotient, such as volts over amperes.
 * @param[in] first   Unit of the dividend.
 * @param[in] second  Unit of the divisor.
 * @return  The unit with the powers of the divisor taken from those of the dividend; unknown when either is.
 */
Dimension DivideUnits(const Dimension first, const Dimension second) {
    return Combine(first, second, -1);
}

/**
 * @brief   Gives the unit of a sum or a difference.
 * @details Adding a number to a trace, such as V(1)+1, keeps the unit of the trace.
 * @param[in] first             Unit of the first term.
 * @param[in] first_is_number   Whether the first term is a plain number.
 * @param[in] second            Unit of the second term.
 * @param[in] second_is_number  Whether the second term is a plain number.
 * @return  The unit of the trace when one term is a number, the common unit when both are traces of the same one,
 *          and an unknown unit when they differ.
 */
Dimension
AddUnits(const Dimension first, const bool first_is_number, const Dimension second, const bool second_is_number) {
    if (first_is_number) {
        return second;
    }
    if (second_is_number || first == second) {
        return first;
    }
    return {.Known = false};
}

/**
 * @brief   Gives the unit of a square root.
 * @param[in] unit  Unit of the radicand.
 * @return  No unit for a radicand without one, and an unknown unit for any other, since half a power is not kept.
 */
Dimension GetSquareRootUnit(const Dimension unit) {
    return {.Known = unit == Dimension{}};
}

/**
 * @brief   Names a unit as plot axes and readouts show it.
 * @param[in] unit  Powers of volts, amperes and seconds.
 * @return  "V", "A", "W", "Ohm", "S" or a product such as "V/s" or "A*s"; empty when there is no unit or it is
 *          not known.
 */
std::string FormatUnit(const Dimension unit) {
    if (!unit.Known) {
        return "";
    }
    const std::array<std::pair<Dimension, const char *>, 3> named = {{
        {{.Volts = 1, .Amperes = 1}, "W"},
        {{.Volts = 1, .Amperes = -1}, "Ohm"},
        {{.Volts = -1, .Amperes = 1}, "S"},
    }};
    for (const auto &[dimension, name] : named) {
        if (unit == dimension) {
            return name;
        }
    }
    std::string numerator;
    std::string denominator;
    const std::array<std::pair<int, const char *>, 3> powers = {{
        {unit.Volts, "V"},
        {unit.Amperes, "A"},
        {unit.Seconds, "s"},
    }};
    for (const auto &[power, symbol] : powers) {
        if (power == 0) {
            continue;
        }
        std::string &part = power > 0 ? numerator : denominator;
        if (!part.empty()) {
            part += "*";
        }
        part += symbol;
        if (std::abs(power) > 1) {
            part += std::format("^{}", std::abs(power));
        }
    }
    if (denominator.empty()) {
        return numerator;
    }
    return std::format("{}/{}", numerator.empty() ? "1" : numerator, denominator);
}

} // namespace GUI
