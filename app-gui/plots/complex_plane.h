/**
 * @file    complex_plane.h
 * @brief   Geometry of Polar and Nyquist plots in the complex plane, shared by the Output window and SVG export.
 */

#ifndef IMCSIM_COMPLEX_PLANE_H
#define IMCSIM_COMPLEX_PLANE_H

#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/// Points along a full circle of a polar grid or the Nyquist unit circle.
inline constexpr int COMPLEX_CURVE_SEGMENTS = 96;

/// Critical point (-1 + j0) where a standard negative-feedback loop has zero stability margin.
inline constexpr std::complex<double> NYQUIST_CRITICAL_POINT{-1.0, 0.0};

/**
 * @enum    ComplexPlaneKind
 * @brief   How the complex plane is framed and marked: concentric polar circles or Cartesian Nyquist axes.
 */
enum class ComplexPlaneKind {
    POLAR,
    NYQUIST
};

/**
 * @struct  ComplexPlanePoint
 * @brief   Coordinates of a point on the complex plane, with real to the right and imaginary upward.
 */
struct ComplexPlanePoint {
    double X = 0.0;
    double Y = 0.0;
};

/**
 * @struct  PolarGridCircle
 * @brief   One concentric magnitude circle of a polar grid, with its radius label.
 */
struct PolarGridCircle {
    double Radius = 0.0;
    std::string Label;
    std::vector<ComplexPlanePoint> Points;
};

/**
 * @struct  PolarGridSpoke
 * @brief   One radial angle line of a polar grid from the origin to the outer circle, with its degree label.
 */
struct PolarGridSpoke {
    ComplexPlanePoint End;
    ComplexPlanePoint LabelPosition;
    std::string Label;
};

/**
 * @struct  PolarGrid
 * @brief   Concentric magnitude circles and 30-degree angle spokes of a polar chart.
 */
struct PolarGrid {
    double OuterRadius = 1.0;
    std::vector<PolarGridCircle> Circles;
    std::vector<PolarGridSpoke> Spokes;
};

/**
 * @struct  DirectionArrow
 * @brief   Chevron arrowhead placed along a complex locus in the direction of increasing frequency.
 */
struct DirectionArrow {
    ComplexPlanePoint Tip;
    ComplexPlanePoint LeftWing;
    ComplexPlanePoint RightWing;
};

// Extent and grids
double FindMaxComplexMagnitude(std::span<const std::vector<std::complex<double>>> curves);
double ChooseNyquistExtent(double max_magnitude);
std::vector<ComplexPlanePoint> BuildCirclePoints(double radius, int segments = COMPLEX_CURVE_SEGMENTS);
PolarGrid BuildPolarGrid(double max_magnitude, int segments = COMPLEX_CURVE_SEGMENTS);

// Curves and hit-testing
std::vector<DirectionArrow> BuildDirectionArrows(std::span<const std::complex<double>> values, double arrow_length);
std::vector<std::complex<double>> MirrorNegativeFrequencies(std::span<const std::complex<double>> values);
[[nodiscard]] std::optional<std::size_t> FindNearestComplexPoint(std::span<const std::complex<double>> values,
                                                                 std::complex<double> target);

} // namespace GUI

#endif // IMCSIM_COMPLEX_PLANE_H
