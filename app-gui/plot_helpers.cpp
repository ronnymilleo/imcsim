/**
 * @file    plot_helpers.cpp
 * @brief   Plot geometry that needs no plotting library: nearest samples and dashed lines.
 */

#include "plot_helpers.h"

#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

ImVec2 Lerp(const ImVec2 start, const ImVec2 end, const float fraction) {
    return {start.x + (end.x - start.x) * fraction, start.y + (end.y - start.y) * fraction};
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
 * @brief   Splits a polyline into the visible pieces of a dashed line.
 * @param[in] points       Polyline vertices in screen pixels.
 * @param[in] dash_length  Length of each dash, in pixels.
 * @param[in] gap_length   Length of each gap, in pixels.
 * @return  The dashes, in order. The pattern runs on across vertices, so dashes keep their length around bends.
 */
std::vector<LineSegment> SplitIntoDashes(const std::span<const ImVec2> points, const float dash_length,
                                         const float gap_length) {
    std::vector<LineSegment> dashes;
    const float period = dash_length + gap_length;
    // Distance already covered in the current dash-and-gap period
    float phase = 0.0f;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const ImVec2 start = points[index - 1];
        const ImVec2 end = points[index];
        const float length = std::hypot(end.x - start.x, end.y - start.y);
        float covered = 0.0f;
        while (covered < length) {
            const bool in_dash = phase < dash_length;
            const float left_in_part = in_dash ? dash_length - phase : period - phase;
            const float step = std::min(left_in_part, length - covered);
            if (in_dash) {
                dashes.push_back({Lerp(start, end, covered / length), Lerp(start, end, (covered + step) / length)});
            }
            covered += step;
            phase = std::fmod(phase + step, period);
        }
    }
    return dashes;
}

} // namespace GUI
