/**
 * @file    smith_chart.h
 * @brief   The grid of a Smith chart as curves on the plane of the reflection coefficient, and the map between
 *          impedance and reflection.
 * @details Pure geometry, shared by the chart of the Output window and the SVG export, so both draw the same circles.
 *          The chart lies inside the unit circle of the complex plane; a point is the reflection coefficient, with the
 *          real part to the right and the imaginary part upward.
 */

#ifndef IMCSIM_SMITH_CHART_H
#define IMCSIM_SMITH_CHART_H

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SmithPoint
 * @brief   A point of the reflection plane.
 */
struct SmithPoint {
    double X = 0.0;
    double Y = 0.0;
};

/**
 * @struct  SmithCurve
 * @brief   One curve of the grid, with the label that names it where it meets the edge or the real axis.
 */
struct SmithCurve {
    std::vector<SmithPoint> Points;
    std::string Label;
    SmithPoint LabelPosition;
    /// Whether it is a circle of constant resistance, as against an arc of constant reactance or the outer circle.
    bool IsResistance = false;
};

/// The normalized resistances and reactances the grid draws a curve for, and their negatives for the reactances.
inline constexpr std::array<double, 5> SMITH_GRID_VALUES = {0.2, 0.5, 1.0, 2.0, 5.0};

/// Points of each curve of the default grid.
inline constexpr int SMITH_CURVE_SEGMENTS = 96;

// Grid
std::vector<SmithCurve> BuildSmithGrid(int segments);

// Impedance and reflection
std::complex<double> ToReflection(std::complex<double> normalized_impedance);
std::string FormatSmithValue(double value);

} // namespace GUI

#endif // IMCSIM_SMITH_CHART_H
