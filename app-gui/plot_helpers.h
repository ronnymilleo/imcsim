/**
 * @file    plot_helpers.h
 * @brief   Plot geometry that needs no plotting library: nearest samples and dashed lines.
 */

#ifndef IMCSIM_PLOT_HELPERS_H
#define IMCSIM_PLOT_HELPERS_H

#include "imgui.h"
#include <cstddef>
#include <span>
#include <vector>

namespace GUI {

/**
 * @struct  LineSegment
 * @brief   A straight piece of a line, in screen pixels.
 */
struct LineSegment {
    ImVec2 Start;
    ImVec2 End;
};

std::size_t FindNearestSample(std::span<const double> xs, double x, bool logarithmic);
std::vector<LineSegment> SplitIntoDashes(std::span<const ImVec2> points, float dash_length, float gap_length,
                                         ImVec2 clip_min, ImVec2 clip_max);

} // namespace GUI

#endif // IMCSIM_PLOT_HELPERS_H
