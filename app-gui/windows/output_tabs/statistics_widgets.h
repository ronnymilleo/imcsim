/**
 * @file    statistics_widgets.h
 * @brief   The statistics panels under the plots of the Output window, with the formatting of their values.
 */

#ifndef IMCSIM_STATISTICS_WIDGETS_H
#define IMCSIM_STATISTICS_WIDGETS_H

#include "plots/statistics_table.h"
#include "windows/output_tabs/plot_widgets.h"
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

/// The readings of the cursors themselves, a name and a value each, such as A and its time.
using CursorReadings = std::vector<std::pair<std::string, std::string>>;

// Statistics
std::string FormatQuantity(double value, const char *unit);
std::string FormatFixed(double value, const char *unit);
std::string FormatOptionalQuantity(std::optional<double> value, const char *unit);
void DrawStatistic(const char *name, const std::string &value);
void DrawMeasuredValue(const char *name, double value, double scale, const char *unit);
bool BeginStatisticsTable(const std::string &id);
void DrawStatisticsHeader(const char *span_text);
void DrawTraceTable(const char *id, const std::vector<PlottedTrace> &traces, const std::vector<StatisticsRow> &rows,
                    const CursorReadings &cursor_readings);
void DrawWithStatistics(const std::function<void()> &draw_plots, const std::function<void()> &draw_statistics);

} // namespace GUI

#endif // IMCSIM_STATISTICS_WIDGETS_H
