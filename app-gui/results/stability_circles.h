/**
 * @file    stability_circles.h
 * @brief   The source and load stability circles of a two-port on the plane of the reflection coefficient, and the
 *          lines that shade what they leave unstable.
 * @details Pure geometry, shared by the Smith chart of the Output window and the SVG export. The load circle lies on
 * the plane of the reflection coefficient of the load and the source circle on the one of the source; each is the set
 * of terminations that make the other port reflect exactly 1, and what lies on one side of it makes the port
 * oscillate-prone. The formulas are those of Gonzalez, "Microwave Transistor Amplifiers", section 3.3, written in the
 * general form A |z|^2 - 2 Re(B z) + C = 0, which also holds where the circle is a line.
 */

#ifndef IMCSIM_STABILITY_CIRCLES_H
#define IMCSIM_STABILITY_CIRCLES_H

#include "results/two_port_metrics.h"
#include <complex>
#include <vector>

namespace GUI {

/// The farthest from the center of the chart, in units of its radius, that a boundary is sampled, which is past the
/// largest chart the views draw.
constexpr double BOUNDARY_VIEW_RADIUS = 2.0;

/// Points of a full circle of the boundary; a part of one has the share of them its angle covers.
constexpr int BOUNDARY_FULL_CIRCLE_POINTS = 360;

/// Distance between the lines that shade the unstable region, in units of the radius of the chart.
constexpr double HATCH_SPACING = 0.06;

/// A coefficient |A| of the circle at or below this makes it a line: its radius would pass 1e9 chart radii.
constexpr double LINE_COEFFICIENT_FLOOR = 1e-9;

/**
 * @enum    BoundaryShape
 * @brief   What the edge of the stable region is.
 * @details NONE: there is no edge, because a two-port with no S12 reflects what it reflects whatever is connected, so
 *          every termination is stable or every one is not.
 */
enum class BoundaryShape {
    CIRCLE,
    LINE,
    NONE
};

/**
 * @enum    ChartCoverage
 * @brief   How the boundary divides the chart, the unit circle of the reflection plane.
 */
enum class ChartCoverage {
    PARTLY,
    ENTIRELY_STABLE,
    ENTIRELY_UNSTABLE
};

/**
 * @struct  StabilityBoundary
 * @brief   The edge between the stable and the unstable terminations on one plane, and how it lies over the chart.
 */
struct StabilityBoundary {
    BoundaryShape Shape = BoundaryShape::NONE;
    std::complex<double> Center;       ///< The center of a circle, or the point of a line nearest to the origin.
    double Radius = 0.0;               ///< The radius of a circle.
    std::complex<double> StableNormal; ///< For a line, the unit vector from it toward the stable side.
    bool IsStableInside = false; ///< For a circle, whether the stable terminations are inside it, as against outside.
    ChartCoverage Coverage = ChartCoverage::ENTIRELY_STABLE;
};

/**
 * @struct  StabilityCircles
 * @brief   The two boundaries of a two-port at one frequency.
 */
struct StabilityCircles {
    StabilityBoundary Source; ///< On the plane of the reflection coefficient of the source.
    StabilityBoundary Load;   ///< On the plane of the reflection coefficient of the load.
};

/**
 * @struct  HatchSegment
 * @brief   One stroke of the hatching of an unstable region, on the reflection plane.
 */
struct HatchSegment {
    std::complex<double> From;
    std::complex<double> To;
};

// Boundaries
StabilityCircles ComputeStabilityCircles(const TwoPortMatrix &matrix);

// Drawing
std::vector<std::vector<std::complex<double>>> SampleBoundary(const StabilityBoundary &boundary);
std::vector<HatchSegment> BuildUnstableHatch(const StabilityBoundary &boundary);

} // namespace GUI

#endif // IMCSIM_STABILITY_CIRCLES_H
