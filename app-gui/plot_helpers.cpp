/**
 * @file    plot_helpers.cpp
 * @brief   Plot geometry that needs no plotting library: nearest samples, curve spread and dashed lines.
 */

#include "plot_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GUI {

namespace {

/**
 * @struct  PixelPoint
 * @brief   A point in screen pixels kept in double precision, since off-screen samples can be very far away.
 */
struct PixelPoint {
    double X;
    double Y;
};

PixelPoint Lerp(const PixelPoint start, const PixelPoint end, const double fraction) {
    return {start.X + (end.X - start.X) * fraction, start.Y + (end.Y - start.Y) * fraction};
}

ImVec2 ToVec2(const PixelPoint point) {
    return {static_cast<float>(point.X), static_cast<float>(point.Y)};
}

// Narrows [first, last], the part of a segment still inside, by one edge of the rectangle (Liang-Barsky). The
// segment is start + fraction * delta, and offset is how far start is inside the edge; returns false once nothing
// is left
bool ClipEdge(const double delta, const double offset, double &first, double &last) {
    if (delta == 0.0) {
        return offset >= 0.0;
    }
    const double fraction = -offset / delta;
    if (delta > 0.0) {
        first = std::max(first, fraction);
    } else {
        last = std::min(last, fraction);
    }
    return first <= last;
}

} // namespace

/**
 * @brief   Finds the sample closest to an X value, such as the one under the cursor.
 * @param[in] xs           X values of the samples, rising or falling; a DC sweep may run downward.
 * @param[in] x            X value to look for.
 * @param[in] logarithmic  Compares distances in decades, as a logarithmic axis shows them; the values must then be
 *                         positive.
 * @return  Index of the nearest sample, or 0 when there are no samples.
 */
std::size_t FindNearestSample(const std::span<const double> xs, const double x, const bool logarithmic) {
    const auto distance = [&](const double value) {
        return logarithmic ? std::abs(std::log10(value) - std::log10(x)) : std::abs(value - x);
    };
    std::size_t nearest = 0;
    for (std::size_t index = 1; index < xs.size(); ++index) {
        if (distance(xs[index]) < distance(xs[nearest])) {
            nearest = index;
        }
    }
    return nearest;
}

/**
 * @brief   Finds where a family of curves is most spread apart, such as the best place to label each curve.
 * @param[in] curves  Values of each curve, all sampled at the same X values.
 * @return  Index of the sample where the highest and lowest curves are furthest apart; the last of the widest
 *          when several tie, so curves that only part at the end are labeled there. Values that are not finite
 *          are ignored, and an empty family gives 0.
 */
std::size_t FindWidestSpread(const std::vector<const std::vector<double> *> &curves) {
    std::size_t sample_count = 0;
    for (const std::vector<double> *curve : curves) {
        sample_count = std::max(sample_count, curve->size());
    }
    std::size_t widest = sample_count == 0 ? 0 : sample_count - 1;
    double widest_spread = -1.0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
        for (const std::vector<double> *curve : curves) {
            if (index < curve->size() && std::isfinite((*curve)[index])) {
                lowest = std::min(lowest, (*curve)[index]);
                highest = std::max(highest, (*curve)[index]);
            }
        }
        if (highest >= lowest && highest - lowest >= widest_spread) {
            widest_spread = highest - lowest;
            widest = index;
        }
    }
    return widest;
}

/**
 * @brief   Splits a polyline into the dashes of a dashed line, keeping only what falls inside a rectangle.
 * @param[in] points       Polyline vertices in screen pixels.
 * @param[in] dash_length  Length of each dash, in pixels.
 * @param[in] gap_length   Length of each gap, in pixels.
 * @param[in] clip_min     Top-left corner of the visible area, such as the plot.
 * @param[in] clip_max     Bottom-right corner of the visible area.
 * @return  The dashes, in order. The pattern runs on across vertices, so dashes keep their length around bends,
 *          and across the clipped parts, so dashes do not shift while the view pans.
 * @note    The work depends on the visible length only: a zoomed-in plot puts samples millions of pixels away,
 *          and dashing those whole segments would take all the memory. Segments with a coordinate that is not
 *          finite are skipped.
 */
std::vector<LineSegment> SplitIntoDashes(const std::span<const ImVec2> points, const float dash_length,
                                         const float gap_length, const ImVec2 clip_min, const ImVec2 clip_max) {
    std::vector<LineSegment> dashes;
    const double period = static_cast<double>(dash_length) + gap_length;
    // Distance already covered in the current dash-and-gap period
    double phase = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const PixelPoint start = {points[index - 1].x, points[index - 1].y};
        const PixelPoint end = {points[index].x, points[index].y};
        const double delta_x = end.X - start.X;
        const double delta_y = end.Y - start.Y;
        const double length = std::hypot(delta_x, delta_y);
        if (!std::isfinite(length) || length == 0.0) {
            continue;
        }
        double first = 0.0;
        double last = 1.0;
        const bool visible = ClipEdge(delta_x, start.X - clip_min.x, first, last) &&
                             ClipEdge(-delta_x, clip_max.x - start.X, first, last) &&
                             ClipEdge(delta_y, start.Y - clip_min.y, first, last) &&
                             ClipEdge(-delta_y, clip_max.y - start.Y, first, last);
        if (!visible) {
            phase = std::fmod(phase + length, period);
            continue;
        }
        phase = std::fmod(phase + first * length, period);
        double covered = first * length;
        const double visible_end = last * length;
        while (covered < visible_end) {
            const bool in_dash = phase < dash_length;
            const double left_in_part = in_dash ? dash_length - phase : period - phase;
            const double step = std::min(left_in_part, visible_end - covered);
            if (in_dash) {
                dashes.push_back(
                    {ToVec2(Lerp(start, end, covered / length)), ToVec2(Lerp(start, end, (covered + step) / length))});
            }
            covered += step;
            phase = std::fmod(phase + step, period);
        }
        phase = std::fmod(phase + (length - visible_end), period);
    }
    return dashes;
}

} // namespace GUI
