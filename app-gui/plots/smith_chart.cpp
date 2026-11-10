/**
 * @file    smith_chart.cpp
 * @brief   The grid of a Smith chart as curves on the plane of the reflection coefficient, and the map between
 *          impedance and reflection.
 */

#include "plots/smith_chart.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace GUI {

namespace {

constexpr double HALF_PI = std::numbers::pi / 2.0;

SmithPoint ToPoint(const std::complex<double> reflection) {
    return {reflection.real(), reflection.imag()};
}

/**
 * @brief   The circle of constant resistance r: centered at r / (1 + r) on the real axis, with radius 1 / (1 + r).
 */
SmithCurve BuildResistanceCircle(const double resistance, const int segments) {
    SmithCurve curve;
    curve.IsResistance = true;
    const double radius = 1.0 / (1.0 + resistance);
    const double center = resistance / (1.0 + resistance);
    for (int step = 0; step <= segments; ++step) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(step) / static_cast<double>(segments);
        curve.Points.push_back({center + radius * std::cos(angle), radius * std::sin(angle)});
    }
    curve.Label = FormatSmithValue(resistance);
    // The circle crosses the real axis at (r - 1) / (r + 1), on its left
    curve.LabelPosition = {(resistance - 1.0) / (resistance + 1.0), 0.0};
    return curve;
}

/**
 * @brief   The arc of constant reactance x, from the outer circle (resistance 0) to the open circuit (resistance
 *          infinite), as the reflection of r + jx.
 * @details The resistance runs as the tangent of an angle, so the samples crowd where the arc bends most.
 */
SmithCurve BuildReactanceArc(const double reactance, const int segments) {
    SmithCurve curve;
    for (int step = 0; step < segments; ++step) {
        const double angle = HALF_PI * static_cast<double>(step) / static_cast<double>(segments);
        curve.Points.push_back(ToPoint(ToReflection({std::tan(angle), reactance})));
    }
    // The last sample is the open circuit, where the tangent has no value
    curve.Points.push_back({1.0, 0.0});
    curve.Label = (reactance > 0.0 ? "+j" : "-j") + FormatSmithValue(std::abs(reactance));
    curve.LabelPosition = curve.Points.front();
    return curve;
}

SmithCurve BuildOuterCircle(const int segments) {
    SmithCurve curve;
    for (int step = 0; step <= segments; ++step) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(step) / static_cast<double>(segments);
        curve.Points.push_back({std::cos(angle), std::sin(angle)});
    }
    return curve;
}

} // namespace

/**
 * @brief   Builds the curves of the grid: the outer circle, the circles of constant resistance and the arcs of
 *          constant reactance, positive above the real axis and negative below.
 * @param[in] segments  Points of each curve; at least 4.
 * @return  The outer circle first, then the resistance circles for SMITH_GRID_VALUES, then the arcs, each reactance
 *          with its negative. Every point lies on or inside the unit circle.
 */
std::vector<SmithCurve> BuildSmithGrid(const int segments) {
    const int count = std::max(segments, 4);
    std::vector<SmithCurve> grid;
    grid.push_back(BuildOuterCircle(count));
    for (const double resistance : SMITH_GRID_VALUES) {
        grid.push_back(BuildResistanceCircle(resistance, count));
    }
    for (const double reactance : SMITH_GRID_VALUES) {
        grid.push_back(BuildReactanceArc(reactance, count));
        grid.push_back(BuildReactanceArc(-reactance, count));
    }
    return grid;
}

/**
 * @brief   Maps a normalized impedance to its reflection coefficient.
 * @param[in] normalized_impedance  Impedance divided by the reference impedance.
 * @return  (z - 1) / (z + 1).
 */
std::complex<double> ToReflection(const std::complex<double> normalized_impedance) {
    return (normalized_impedance - 1.0) / (normalized_impedance + 1.0);
}

/**
 * @brief   Writes a grid value without trailing zeros.
 * @param[in] value  A value of SMITH_GRID_VALUES.
 * @return  Such as "0.2", "1" or "5".
 */
std::string FormatSmithValue(const double value) {
    return std::format("{}", value);
}

} // namespace GUI
