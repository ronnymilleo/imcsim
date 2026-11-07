/**
 * @file    statistics_table.h
 * @brief   The statistics of the plotted traces as tables with a column per trace.
 */

#ifndef IMCSIM_STATISTICS_TABLE_H
#define IMCSIM_STATISTICS_TABLE_H

#include "plots/plot_types.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  MeasuredTrace
 * @brief   A trace the statistics read: its values at the times of the transient and its unit.
 */
struct MeasuredTrace {
    std::span<const double> Values;
    std::string_view Unit;
};

/**
 * @struct  StatisticsCell
 * @brief   One reading of a trace, as the table writes it.
 */
struct StatisticsCell {
    std::string Text;
    /// The exact value of a reading written "~0", or empty.
    std::string Tooltip;
};

/**
 * @struct  StatisticsRow
 * @brief   One quantity across the traces, such as the RMS of each, in the order of the traces.
 */
struct StatisticsRow {
    std::string Name;
    std::vector<StatisticsCell> Cells;
};

/**
 * @struct  BodeTrace
 * @brief   A trace of an AC sweep: its magnitude in decibels and its phase in degrees, at the swept frequencies.
 */
struct BodeTrace {
    std::span<const double> MagnitudesDecibels;
    std::span<const double> PhasesDegrees;
};

/**
 * @struct  SweptTrace
 * @brief   A curve of a DC sweep, at the swept values, and its unit.
 */
struct SweptTrace {
    std::span<const double> Values;
    std::string_view Unit;
};

std::vector<StatisticsRow> BuildTransientStatistics(std::span<const double> times,
                                                    const std::vector<MeasuredTrace> &traces, PlotSpan span,
                                                    const std::optional<PlotCursors> &cursors);
std::vector<StatisticsRow> BuildBodeStatistics(std::span<const double> frequencies,
                                               const std::vector<BodeTrace> &traces,
                                               const std::optional<PlotCursors> &cursors);
std::vector<StatisticsRow> BuildSweepStatistics(std::span<const double> swept, std::string_view swept_unit,
                                                const std::vector<SweptTrace> &traces, PlotSpan span,
                                                const std::optional<PlotCursors> &cursors);

} // namespace GUI

#endif // IMCSIM_STATISTICS_TABLE_H
