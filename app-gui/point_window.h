/**
 * @file    point_window.h
 * @brief   The stretch of a long transient that the plot shows, so drawing and measuring cost the same on any run.
 */

#ifndef IMCSIM_POINT_WINDOW_H
#define IMCSIM_POINT_WINDOW_H

#include "math_channels.h"
#include "simulation_results.h"
#include <cstddef>
#include <vector>

namespace GUI {

// How many points the transient plot shows at once, however long the run is: the plot, its axes fit and its
// statistics all go over the points in view, and a scroll bar moves the window along the run. The user picks the size
// between the smallest and the largest
constexpr int DEFAULT_WINDOW_POINTS = 2000;
constexpr int MIN_WINDOW_POINTS = 1000;
constexpr int MAX_WINDOW_POINTS = 10000;

/**
 * @struct  PointWindow
 * @brief   A run of consecutive points of a result, by the index of the first and how many there are.
 */
struct PointWindow {
    std::size_t First = 0;
    std::size_t Count = 0;
};

// Choosing the window
PointWindow ClampWindow(std::size_t total_points, std::size_t first, std::size_t window_points);
PointWindow GetEndWindow(std::size_t total_points, std::size_t window_points);

// Cutting a result
Core::Transient SliceTransient(const Core::Transient &transient, PointWindow window);
Core::Transient SliceTransientByTime(const Core::Transient &transient, double from, double to);
std::vector<MathTrace> SliceMathTraces(const std::vector<MathTrace> &traces, PointWindow window);

} // namespace GUI

#endif // IMCSIM_POINT_WINDOW_H
