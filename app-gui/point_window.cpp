/**
 * @file    point_window.cpp
 * @brief   The stretch of a long transient that the plot shows, so drawing and measuring cost the same on any run.
 */

#include "point_window.h"

#include <algorithm>
#include <utility>

namespace GUI {

/**
 * @brief   Fits a window of the size the plot shows inside a result.
 * @param[in] total_points   Number of points in the result.
 * @param[in] first          Index the window should start at.
 * @param[in] window_points  How many points the plot shows.
 * @return  The window that starts at first, or as late as it can while keeping its size; every point when the result
 *          has no more than the plot shows.
 */
PointWindow ClampWindow(const std::size_t total_points, const std::size_t first, const std::size_t window_points) {
    const std::size_t count = std::min(total_points, window_points);
    return {.First = std::min(first, total_points - count), .Count = count};
}

/**
 * @brief   Gives the window over the newest points of a result.
 * @param[in] total_points   Number of points in the result.
 * @param[in] window_points  How many points the plot shows.
 * @return  The last points, as many as the plot shows.
 */
PointWindow GetEndWindow(const std::size_t total_points, const std::size_t window_points) {
    return ClampWindow(total_points, total_points, window_points);
}

/**
 * @brief   Copies a run of consecutive points of a transient.
 * @param[in] transient  Transient to cut.
 * @param[in] window     Points to keep, which ClampWindow() fitted inside it.
 * @return  A transient with only those points of every time, node voltage and current, and the warnings and
 *          completeness of the original.
 */
Core::Transient SliceTransient(const Core::Transient &transient, const PointWindow window) {
    const auto cut = [&window](const std::vector<double> &values) {
        const auto first = values.begin() + static_cast<std::ptrdiff_t>(window.First);
        return std::vector<double>(first, first + static_cast<std::ptrdiff_t>(window.Count));
    };
    Core::Transient slice{.Times = cut(transient.Times),
                          .NodeVoltages = {},
                          .Currents = {},
                          .Warnings = transient.Warnings,
                          .IsComplete = transient.IsComplete};
    for (const std::vector<double> &node : transient.NodeVoltages) {
        slice.NodeVoltages.push_back(cut(node));
    }
    for (const Core::ComponentTrace &current : transient.Currents) {
        slice.Currents.push_back({current.Name, cut(current.Values)});
    }
    return slice;
}

/**
 * @brief   Copies the points of a transient within a span of time, so a window of one run can be drawn with the
 *          steps of the same analysis, whose points fall at other times.
 * @param[in] transient  Transient to cut.
 * @param[in] from       Start of the span in seconds.
 * @param[in] to         End of the span in seconds.
 * @return  The points from one before the span to one after it, so the lines reach its edges; none when the
 *          transient has no points.
 */
Core::Transient SliceTransientByTime(const Core::Transient &transient, const double from, const double to) {
    if (transient.Times.empty()) {
        return transient;
    }
    const auto before = std::ranges::lower_bound(transient.Times, from);
    const auto after = std::ranges::upper_bound(transient.Times, to);
    const std::size_t first =
        before == transient.Times.begin() ? 0 : static_cast<std::size_t>(before - transient.Times.begin()) - 1;
    const std::size_t last =
        std::min(transient.Times.size(), static_cast<std::size_t>(after - transient.Times.begin()) + 1);
    return SliceTransient(transient, {.First = first, .Count = last > first ? last - first : 0});
}

/**
 * @brief   Copies the values of math traces that fall in a window, so channels worked out over a whole run can be
 *          drawn and measured over a stretch of it.
 * @param[in] traces  Math traces over the whole run.
 * @param[in] window  Points to keep; values the trace lacks, as when it was worked out over a shorter run, are left
 * out.
 * @return  The same traces, with the values of the window only.
 */
std::vector<MathTrace> SliceMathTraces(const std::vector<MathTrace> &traces, const PointWindow window) {
    std::vector<MathTrace> slices;
    for (const MathTrace &trace : traces) {
        MathTrace slice{
            .Label = trace.Label, .Values = {}, .Unit = trace.Unit, .Axis = trace.Axis, .Color = trace.Color};
        const std::size_t first = std::min(window.First, trace.Values.size());
        const std::size_t last = std::min(window.First + window.Count, trace.Values.size());
        slice.Values.assign(trace.Values.begin() + static_cast<std::ptrdiff_t>(first),
                            trace.Values.begin() + static_cast<std::ptrdiff_t>(last));
        slices.push_back(std::move(slice));
    }
    return slices;
}

} // namespace GUI
