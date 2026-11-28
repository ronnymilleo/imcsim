/**
 * @file    complex_plane.cpp
 * @brief   Geometry of Polar and Nyquist plots in the complex plane, shared by the Output window and SVG export.
 */

#include "plots/complex_plane.h"

#include "plots/plot_ticks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace GUI {

namespace {

constexpr double NYQUIST_MIN_EXTENT = 1.15;
constexpr double EXTENT_MARGIN = 1.08;
constexpr double POLAR_SPOKE_LABEL_SCALE = 1.08;
constexpr double ARROW_WING_ANGLE = 5.0 * std::numbers::pi / 6.0;
constexpr int TARGET_POLAR_CIRCLES = 4;
constexpr std::array<int, 12> SPOKE_DEGREES = {0, 30, 60, 90, 120, 150, 180, -150, -120, -90, -60, -30};

bool IsFiniteComplex(const std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

/**
 * @brief   Places one chevron arrowhead at `target_distance` along the polyline arc length.
 */
std::optional<DirectionArrow> PlaceArrowAtArcLength(const std::span<const std::complex<double>> values,
                                                    const double target_distance,
                                                    const double arrow_length) {
    double walked = 0.0;
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (!IsFiniteComplex(values[index - 1]) || !IsFiniteComplex(values[index])) {
            continue;
        }
        const std::complex<double> delta = values[index] - values[index - 1];
        const double seg_len = std::abs(delta);
        if (seg_len <= 0.0) {
            continue;
        }
        if (walked + seg_len >= target_distance) {
            const double fraction = std::clamp((target_distance - walked) / seg_len, 0.0, 1.0);
            const std::complex<double> tip = values[index - 1] + fraction * delta;
            const double heading = std::atan2(delta.imag(), delta.real());
            const std::complex<double> left = tip + std::polar(arrow_length, heading + ARROW_WING_ANGLE);
            const std::complex<double> right = tip + std::polar(arrow_length, heading - ARROW_WING_ANGLE);
            return DirectionArrow{.Tip = {tip.real(), tip.imag()},
                                  .LeftWing = {left.real(), left.imag()},
                                  .RightWing = {right.real(), right.imag()}};
        }
        walked += seg_len;
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Finds the largest finite magnitude across a set of complex curves.
 * @param[in] curves  Complex traces to inspect.
 * @return  Largest finite `|z|`, or `0.0` when all curves are empty or non-finite.
 */
double FindMaxComplexMagnitude(const std::span<const std::vector<std::complex<double>>> curves) {
    double largest = 0.0;
    for (const std::vector<std::complex<double>> &curve : curves) {
        for (const std::complex<double> value : curve) {
            if (IsFiniteComplex(value)) {
                largest = std::max(largest, std::abs(value));
            }
        }
    }
    return largest;
}

/**
 * @brief   Half-width of the initial Nyquist square, always framing both the unit circle and `(-1, 0)`.
 * @param[in] max_magnitude  Largest finite magnitude among the plotted curves.
 * @return  Positive half-extent with margin around the curves and unit circle.
 */
double ChooseNyquistExtent(const double max_magnitude) {
    const double safe_max = std::isfinite(max_magnitude) && max_magnitude > 0.0 ? max_magnitude : 1.0;
    return std::max(NYQUIST_MIN_EXTENT, safe_max * EXTENT_MARGIN);
}

/**
 * @brief   Samples a closed circle centered at the origin.
 * @param[in] radius    Circle radius.
 * @param[in] segments  Number of polygon segments; clamped to at least 8.
 * @return  Closed ring of `segments + 1` points.
 */
std::vector<ComplexPlanePoint> BuildCirclePoints(const double radius, const int segments) {
    const int count = std::max(segments, 8);
    std::vector<ComplexPlanePoint> points;
    points.reserve(static_cast<std::size_t>(count + 1));
    for (int step = 0; step <= count; ++step) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(step) / static_cast<double>(count);
        points.push_back({radius * std::cos(angle), radius * std::sin(angle)});
    }
    return points;
}

/**
 * @brief   Builds a polar grid of round concentric magnitude circles and 30-degree radial spokes.
 * @param[in] max_magnitude  Largest magnitude among the plotted curves; `0` defaults to a unit grid.
 * @param[in] segments       Polygon segments per circle; clamped to at least 8.
 * @return  Concentric circles up to a round outer radius covering `max_magnitude`, plus 12 spokes.
 */
PolarGrid BuildPolarGrid(const double max_magnitude, const int segments) {
    const double target_top = (std::isfinite(max_magnitude) && max_magnitude > 0.0) ? max_magnitude * 1.02 : 1.0;
    std::vector<double> ticks = FindNiceTicks(0.0, target_top, TARGET_POLAR_CIRCLES);
    std::erase_if(ticks, [](const double tick) { return tick <= 0.0; });
    if (ticks.empty()) {
        ticks.push_back(target_top);
    } else if (ticks.back() < target_top) {
        const double step = ticks.size() >= 2 ? (ticks[1] - ticks[0]) : ticks.front();
        ticks.push_back(ticks.back() + step);
    }

    const double step = ticks.size() >= 2 ? (ticks[1] - ticks[0]) : ticks.front();
    PolarGrid grid;
    grid.OuterRadius = ticks.back();
    for (const double radius : ticks) {
        grid.Circles.push_back(
            {.Radius = radius, .Label = FormatTick(radius, step), .Points = BuildCirclePoints(radius, segments)});
    }
    for (const int degrees : SPOKE_DEGREES) {
        const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
        const double cos_a = std::cos(radians);
        const double sin_a = std::sin(radians);
        grid.Spokes.push_back({.End = {grid.OuterRadius * cos_a, grid.OuterRadius * sin_a},
                               .LabelPosition = {grid.OuterRadius * POLAR_SPOKE_LABEL_SCALE * cos_a,
                                                 grid.OuterRadius * POLAR_SPOKE_LABEL_SCALE * sin_a},
                               .Label = std::format("{}°", degrees)});
    }
    return grid;
}

/**
 * @brief   Builds one or two direction arrowheads along a complex locus in the direction of increasing frequency.
 * @param[in] values        Complex locus ordered by increasing frequency.
 * @param[in] arrow_length  Wing length in complex-plane units.
 * @return  Zero, one or two chevrons depending on whether the locus has enough arc length.
 */
std::vector<DirectionArrow> BuildDirectionArrows(const std::span<const std::complex<double>> values,
                                                 const double arrow_length) {
    if (arrow_length <= 0.0 || values.size() < 2) {
        return {};
    }
    double total_length = 0.0;
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (IsFiniteComplex(values[index - 1]) && IsFiniteComplex(values[index])) {
            total_length += std::abs(values[index] - values[index - 1]);
        }
    }
    std::vector<DirectionArrow> arrows;
    if (total_length >= 4.0 * arrow_length) {
        for (const double fraction : {0.35, 0.7}) {
            if (const auto arrow = PlaceArrowAtArcLength(values, total_length * fraction, arrow_length)) {
                arrows.push_back(*arrow);
            }
        }
    } else if (total_length >= 1.5 * arrow_length) {
        if (const auto arrow = PlaceArrowAtArcLength(values, total_length * 0.5, arrow_length)) {
            arrows.push_back(*arrow);
        }
    }
    return arrows;
}

/**
 * @brief   Builds the negative-frequency conjugate branch \f$\overline{H(j\omega)}\f$ running from high frequency to
 * DC.
 * @param[in] values  Positive-frequency complex response ordered by increasing frequency.
 * @return  Conjugated samples in reverse order so the contour runs toward zero frequency.
 */
std::vector<std::complex<double>> MirrorNegativeFrequencies(const std::span<const std::complex<double>> values) {
    std::vector<std::complex<double>> mirrored;
    mirrored.reserve(values.size());
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        mirrored.push_back(std::conj(*it));
    }
    return mirrored;
}

/**
 * @brief   Finds the index of the finite sample closest to `target` in Euclidean complex-plane distance.
 * @param[in] values  Complex samples, which may contain non-finite gaps.
 * @param[in] target  Query point on the complex plane.
 * @return  Index of the nearest finite sample, or `std::nullopt` when no sample is finite.
 */
std::optional<std::size_t> FindNearestComplexPoint(const std::span<const std::complex<double>> values,
                                                   const std::complex<double> target) {
    double best = std::numeric_limits<double>::infinity();
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!IsFiniteComplex(values[index])) {
            continue;
        }
        const double distance = std::abs(values[index] - target);
        if (distance < best) {
            best = distance;
            found = index;
        }
    }
    return found;
}

} // namespace GUI
