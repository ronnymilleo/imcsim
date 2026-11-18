/**
 * @file    stability_circles.cpp
 * @brief   The source and load stability circles of a two-port on the plane of the reflection coefficient, and the
 *          lines that shade what they leave unstable.
 */

#include "results/stability_circles.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace GUI {

namespace {

using Complex = std::complex<double>;

/// A hatch stroke shorter than this, in units of the chart radius, is a point.
constexpr double MIN_STROKE = 1e-9;
/// Fewest points of a part of a circle, so a long arc of a big circle stays round.
constexpr int MIN_ARC_POINTS = 48;
/// A pole this close to the origin, in reflection coefficient, is the one of a port that reflects nothing.
constexpr double ZERO_REFLECTION = 1e-12;

/// The hatching runs along this direction and steps along the one across, as unit vectors of the plane.
const Complex HATCH_ALONG = {std::numbers::sqrt2 / 2.0, std::numbers::sqrt2 / 2.0};
const Complex HATCH_ACROSS = {-std::numbers::sqrt2 / 2.0, std::numbers::sqrt2 / 2.0};

/**
 * @struct  Interval
 * @brief   A stretch of a hatch line, by its distance along the line.
 */
struct Interval {
    double From = 0.0;
    double To = 0.0;
};

/// The scalar product of two points taken as vectors of the plane.
double Dot(const Complex first, const Complex second) {
    return std::real(std::conj(first) * second);
}

ChartCoverage CoverageOfCircle(const StabilityBoundary &boundary) {
    const double distance = std::abs(boundary.Center);
    const bool is_chart_inside = distance + 1.0 <= boundary.Radius;
    const bool is_chart_outside = distance >= 1.0 + boundary.Radius;
    if (is_chart_outside) {
        return boundary.IsStableInside ? ChartCoverage::ENTIRELY_UNSTABLE : ChartCoverage::ENTIRELY_STABLE;
    }
    if (is_chart_inside) {
        return boundary.IsStableInside ? ChartCoverage::ENTIRELY_STABLE : ChartCoverage::ENTIRELY_UNSTABLE;
    }
    return ChartCoverage::PARTLY;
}

ChartCoverage CoverageOfLine(const StabilityBoundary &boundary) {
    // How far the origin lies on the stable side of the line, which is the signed distance to it
    const double origin_side = -Dot(boundary.StableNormal, boundary.Center);
    if (origin_side >= 1.0) {
        return ChartCoverage::ENTIRELY_STABLE;
    }
    if (origin_side <= -1.0) {
        return ChartCoverage::ENTIRELY_UNSTABLE;
    }
    return ChartCoverage::PARTLY;
}

/**
 * @brief   The boundary on the plane of a termination whose far side is the port it sees through the two-port.
 * @details For the load, near is S11 and far is S22: the reflection the source sees is (S11 - Delta z) / (1 - S22 z)
 *          for a load z, and it reads 1 on the boundary. The pole of that ratio, z = 1 / S22, makes the port reflect
 *          an infinite wave, so it is always on the unstable side, which tells the sides apart without a test point
 *          that could lie on the boundary.
 */
StabilityBoundary
FindBoundary(const Complex near_side, const Complex far_side, const Complex delta, const double product) {
    StabilityBoundary boundary;
    if (product <= UNILATERAL_PRODUCT_FLOOR) {
        boundary.Coverage =
            std::abs(near_side) < 1.0 ? ChartCoverage::ENTIRELY_STABLE : ChartCoverage::ENTIRELY_UNSTABLE;
        return boundary;
    }
    const double quadratic = std::norm(delta) - std::norm(far_side);
    const Complex linear = std::conj(near_side) * delta - far_side;
    const double constant = std::norm(near_side) - 1.0;
    const bool has_pole = std::abs(far_side) > ZERO_REFLECTION;
    const Complex pole = has_pole ? 1.0 / far_side : Complex();
    if (std::abs(quadratic) > LINE_COEFFICIENT_FLOOR) {
        boundary.Shape = BoundaryShape::CIRCLE;
        boundary.Center = std::conj(linear) / quadratic;
        boundary.Radius = product / std::abs(quadratic);
        // Without a pole it sits at infinity, outside every circle
        boundary.IsStableInside = !has_pole || std::abs(pole - boundary.Center) > boundary.Radius;
        boundary.Coverage = CoverageOfCircle(boundary);
        return boundary;
    }
    boundary.Shape = BoundaryShape::LINE;
    const Complex normal = std::conj(linear) / std::abs(linear);
    const double offset = constant / (2.0 * std::abs(linear));
    boundary.Center = normal * offset;
    boundary.StableNormal = Dot(normal, pole) > offset ? -normal : normal;
    boundary.Coverage = CoverageOfLine(boundary);
    return boundary;
}

/// The part of the unit chart on one hatch line that lies on the unstable side of a circle.
std::vector<Interval> CutByCircle(const StabilityBoundary &boundary, const double across, const Interval chart) {
    const double circle_across = Dot(HATCH_ACROSS, boundary.Center);
    const double circle_along = Dot(HATCH_ALONG, boundary.Center);
    const double room = boundary.Radius * boundary.Radius - (across - circle_across) * (across - circle_across);
    const double half_chord = room > 0.0 ? std::sqrt(room) : -1.0;
    const bool has_chord = half_chord >= 0.0;
    const Interval inside{.From = circle_along - half_chord, .To = circle_along + half_chord};
    if (boundary.IsStableInside) {
        // The unstable part is what the chart has outside the chord, on either side of it
        if (!has_chord || inside.To <= chart.From || inside.From >= chart.To) {
            return {chart};
        }
        std::vector<Interval> outside;
        if (inside.From > chart.From) {
            outside.push_back({.From = chart.From, .To = inside.From});
        }
        if (inside.To < chart.To) {
            outside.push_back({.From = inside.To, .To = chart.To});
        }
        return outside;
    }
    if (!has_chord) {
        return {};
    }
    const Interval shared{.From = std::max(chart.From, inside.From), .To = std::min(chart.To, inside.To)};
    return shared.To > shared.From ? std::vector<Interval>{shared} : std::vector<Interval>{};
}

/// The part of the unit chart on one hatch line that lies on the unstable side of a line.
std::vector<Interval> CutByLine(const StabilityBoundary &boundary, const double across, const Interval chart) {
    const double normal_across = Dot(HATCH_ACROSS, boundary.StableNormal);
    const double normal_along = Dot(HATCH_ALONG, boundary.StableNormal);
    const double offset = Dot(boundary.StableNormal, boundary.Center);
    // The point is unstable where across * normal_across + along * normal_along - offset <= 0
    const double rest = offset - across * normal_across;
    if (std::abs(normal_along) < ZERO_REFLECTION) {
        return rest >= 0.0 ? std::vector<Interval>{chart} : std::vector<Interval>{};
    }
    const double limit = rest / normal_along;
    const Interval shared = normal_along > 0.0 ? Interval{.From = chart.From, .To = std::min(chart.To, limit)}
                                               : Interval{.From = std::max(chart.From, limit), .To = chart.To};
    return shared.To > shared.From ? std::vector<Interval>{shared} : std::vector<Interval>{};
}

std::vector<Interval>
FindUnstableStretches(const StabilityBoundary &boundary, const double across, const Interval chart) {
    switch (boundary.Shape) {
    case BoundaryShape::CIRCLE:
        return CutByCircle(boundary, across, chart);
    case BoundaryShape::LINE:
        return CutByLine(boundary, across, chart);
    case BoundaryShape::NONE:
    default:
        return boundary.Coverage == ChartCoverage::ENTIRELY_UNSTABLE ? std::vector<Interval>{chart}
                                                                     : std::vector<Interval>{};
    }
}

std::vector<Complex> SampleCircle(const StabilityBoundary &boundary) {
    const double distance = std::abs(boundary.Center);
    const double radius = boundary.Radius;
    constexpr double FULL_TURN = 2.0 * std::numbers::pi;
    double half_angle = std::numbers::pi;
    double first_angle = 0.0;
    if (distance > 0.0) {
        // The point of the circle nearest to the origin is the middle of the part inside the view; the angle is
        // measured from there, where cos(angle) >= q keeps the points within the view radius
        const double q = (distance * distance + radius * radius - BOUNDARY_VIEW_RADIUS * BOUNDARY_VIEW_RADIUS)
                       / (2.0 * distance * radius);
        if (q > 1.0) {
            return {};
        }
        half_angle = q <= -1.0 ? std::numbers::pi : std::acos(q);
        first_angle = std::arg(boundary.Center) + std::numbers::pi;
    } else if (radius > BOUNDARY_VIEW_RADIUS) {
        return {};
    }
    const int points =
        std::max(MIN_ARC_POINTS,
                 static_cast<int>(std::ceil(BOUNDARY_FULL_CIRCLE_POINTS * (2.0 * half_angle) / FULL_TURN)));
    std::vector<Complex> samples;
    samples.reserve(static_cast<std::size_t>(points) + 1);
    for (int index = 0; index <= points; ++index) {
        const double angle = first_angle - half_angle + 2.0 * half_angle * index / points;
        samples.push_back(boundary.Center + std::polar(radius, angle));
    }
    return samples;
}

std::vector<Complex> SampleLine(const StabilityBoundary &boundary) {
    const double room = BOUNDARY_VIEW_RADIUS * BOUNDARY_VIEW_RADIUS - std::norm(boundary.Center);
    if (room <= 0.0) {
        return {};
    }
    const Complex along = Complex(0.0, 1.0) * boundary.StableNormal;
    const double half_length = std::sqrt(room);
    return {boundary.Center - half_length * along, boundary.Center + half_length * along};
}

} // namespace

/**
 * @brief   Works out the source and load stability circles of a two-port.
 * @param[in] matrix  Its S-parameters at one frequency.
 * @return  The boundary on each plane, with the stable side and how it lies over the chart. A two-port with no
 *          transmission back (S12 S21 at the floor) has no boundary; the whole chart is stable on a plane exactly when
 *          the reflection the other port keeps, |S11| for the load plane and |S22| for the source one, is below 1.
 * @details A circle whose coefficient A = |Delta|^2 - |S22|^2 (|S11|^2 - |Delta|^2 for the source) vanishes has an
 *          infinite radius and is a line.
 */
StabilityCircles ComputeStabilityCircles(const TwoPortMatrix &matrix) {
    const Complex delta = matrix.S11 * matrix.S22 - matrix.S12 * matrix.S21;
    const double product = std::abs(matrix.S12 * matrix.S21);
    return {.Source = FindBoundary(matrix.S22, matrix.S11, delta, product),
            .Load = FindBoundary(matrix.S11, matrix.S22, delta, product)};
}

/**
 * @brief   Samples the part of a boundary that lies within BOUNDARY_VIEW_RADIUS of the center of the chart.
 * @param[in] boundary  Boundary on a plane.
 * @return  One polyline in the plane, or none when it lies wholly outside the view or the boundary does not exist.
 *          An arc keeps enough points to look round; a line has its two ends.
 */
std::vector<std::vector<std::complex<double>>> SampleBoundary(const StabilityBoundary &boundary) {
    std::vector<Complex> samples;
    switch (boundary.Shape) {
    case BoundaryShape::CIRCLE:
        samples = SampleCircle(boundary);
        break;
    case BoundaryShape::LINE:
        samples = SampleLine(boundary);
        break;
    case BoundaryShape::NONE:
    default:
        break;
    }
    if (samples.empty()) {
        return {};
    }
    return {std::move(samples)};
}

/**
 * @brief   Lays parallel strokes over the part of the chart that the boundary leaves unstable.
 * @param[in] boundary  Boundary on a plane.
 * @return  Strokes at 45 degrees, HATCH_SPACING apart, inside the unit circle; none when the chart is stable there.
 */
std::vector<HatchSegment> BuildUnstableHatch(const StabilityBoundary &boundary) {
    std::vector<HatchSegment> strokes;
    if (boundary.Coverage == ChartCoverage::ENTIRELY_STABLE) {
        return strokes;
    }
    const int last_line = static_cast<int>(std::floor(1.0 / HATCH_SPACING));
    for (int line = -last_line; line <= last_line; ++line) {
        const double across = line * HATCH_SPACING;
        if (std::abs(across) >= 1.0) {
            continue;
        }
        const double half_chord = std::sqrt(1.0 - across * across);
        for (const Interval stretch :
             FindUnstableStretches(boundary, across, {.From = -half_chord, .To = half_chord})) {
            if (stretch.To - stretch.From > MIN_STROKE) {
                strokes.push_back({.From = across * HATCH_ACROSS + stretch.From * HATCH_ALONG,
                                   .To = across * HATCH_ACROSS + stretch.To * HATCH_ALONG});
            }
        }
    }
    return strokes;
}

} // namespace GUI
