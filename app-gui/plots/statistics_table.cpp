/**
 * @file    statistics_table.cpp
 * @brief   The statistics of the transient traces as one table, with a column per trace.
 */

#include "plots/statistics_table.h"

#include "plots/trace_statistics.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace GUI {

namespace {

/// What a cell reads when the trace has no such value, such as the frequency of a trace that does not repeat.
constexpr std::string_view NO_VALUE = "-";
/// Quantities measured over the span, a row each, in the order the table shows them.
constexpr auto MEASURED_ROWS =
    std::to_array<std::string_view>({"Pk-Pk", "Max", "Min", "Mean", "RMS", "Freq", "Period"});
/// Values at the cursors, a row each after the measured ones.
constexpr auto CURSOR_ROWS = std::to_array<std::string_view>({"At A", "At B", "B-A"});

/**
 * @brief   A reading with its unit; one negligible next to the size of the trace reads "~0", so it is not mistaken for
 *          an exact zero, with the exact value as its tooltip.
 */
StatisticsCell MeasureCell(const double value, const double scale, const std::string_view unit) {
    if (!IsNegligible(value, scale)) {
        return {.Text = std::format("{}{}", Core::FormatMeasurement(value), unit), .Tooltip = {}};
    }
    return {.Text = std::format("~0{}", unit),
            .Tooltip = std::format("{}{}, negligible next to the trace", Core::FormatMeasurement(value), unit)};
}

/**
 * @brief   A reading that is never negligible, such as a frequency, or "-" without one.
 */
StatisticsCell PlainCell(const std::optional<double> value, const std::string_view unit) {
    if (!value) {
        return {.Text = std::string(NO_VALUE), .Tooltip = {}};
    }
    return {.Text = std::format("{}{}", Core::FormatMeasurement(*value), unit), .Tooltip = {}};
}

} // namespace

/**
 * @brief   Measures every trace over a span, like an oscilloscope, and lays the readings out a row per quantity and a
 *          column per trace, so two traces compare across a row.
 * @param[in] times    Times of the transient.
 * @param[in] traces   Traces to measure, each a column in this order.
 * @param[in] span     Span the statistics cover: between the cursors, or what the plot shows.
 * @param[in] cursors  The cursors when shown, which add the value of each trace at both and their difference.
 * @return  The rows Pk-Pk, Max, Min, Mean, RMS, Freq and Period, then At A, At B and B-A with the cursors; a trace
 *          without points in the span reads "-" in the measured rows.
 */
std::vector<StatisticsRow> BuildTransientStatistics(const std::span<const double> times,
                                                    const std::vector<MeasuredTrace> &traces, const PlotSpan span,
                                                    const std::optional<PlotCursors> &cursors) {
    std::vector<StatisticsRow> rows;
    for (const std::string_view name : MEASURED_ROWS) {
        rows.push_back({.Name = std::string(name), .Cells = {}});
    }
    if (cursors) {
        for (const std::string_view name : CURSOR_ROWS) {
            rows.push_back({.Name = std::string(name), .Cells = {}});
        }
    }
    for (const MeasuredTrace &trace : traces) {
        const std::optional<TraceStatistics> statistics =
            ComputeTraceStatistics(times, trace.Values, span.From, span.To);
        // The size of the trace in the span tells residue from real values in every reading
        const double scale = statistics ? std::max(std::abs(statistics->Maximum), std::abs(statistics->Minimum)) : 0.0;
        std::size_t row = 0;
        const auto add = [&rows, &row](StatisticsCell cell) { rows[row++].Cells.push_back(std::move(cell)); };
        if (statistics) {
            add(MeasureCell(statistics->PeakToPeak, scale, trace.Unit));
            add(MeasureCell(statistics->Maximum, scale, trace.Unit));
            add(MeasureCell(statistics->Minimum, scale, trace.Unit));
            add(MeasureCell(statistics->Mean, scale, trace.Unit));
            add(MeasureCell(statistics->RMS, scale, trace.Unit));
            add(PlainCell(statistics->Frequency, "Hz"));
            add(PlainCell(statistics->Frequency ? std::optional(1.0 / *statistics->Frequency) : std::nullopt, "s"));
        } else {
            while (row < MEASURED_ROWS.size()) {
                add(PlainCell(std::nullopt, trace.Unit));
            }
        }
        if (cursors) {
            const double at_a = InterpolateAt(times, trace.Values, cursors->A, false);
            const double at_b = InterpolateAt(times, trace.Values, cursors->B, false);
            add(MeasureCell(at_a, scale, trace.Unit));
            add(MeasureCell(at_b, scale, trace.Unit));
            add(MeasureCell(at_b - at_a, scale, trace.Unit));
        }
    }
    return rows;
}

} // namespace GUI
