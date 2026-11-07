/**
 * @file    statistics_table.cpp
 * @brief   The statistics of the plotted traces as tables with a column per trace.
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
/// What a Bode plot reads over the whole sweep, a row each.
constexpr auto BODE_ROWS =
    std::to_array<std::string_view>({"Peak", "at", "-3 dB low", "-3 dB high", "0 dB at", "Phase margin"});
/// Gain and phase at the cursors of a Bode plot, a row each after those of the whole sweep.
constexpr auto BODE_CURSOR_ROWS =
    std::to_array<std::string_view>({"Gain at A", "Gain at B", "Gain B-A", "Phase at A", "Phase at B", "Phase B-A"});
/// What a DC sweep reads between the cursors, before its span statistics.
constexpr auto SWEEP_CURSOR_ROWS = std::to_array<std::string_view>({"At A", "At B", "B-A", "Slope"});
/// The extremes and mean of a DC sweep over its span, a row each.
constexpr auto SWEEP_ROWS = std::to_array<std::string_view>({"Min", "Max", "Mean"});
/// Decimals of decibels and degrees, which read better fixed than with suffixes.
constexpr int FIXED_DECIMALS = 2;

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

/**
 * @brief   Decibels or degrees with two decimals, or "-" without a value.
 */
StatisticsCell FixedCell(const std::optional<double> value, const std::string_view unit) {
    if (!value) {
        return {.Text = std::string(NO_VALUE), .Tooltip = {}};
    }
    return {.Text = std::format("{}{}", Core::FormatFixed(*value, FIXED_DECIMALS), unit), .Tooltip = {}};
}

/**
 * @brief   Rows with these names and no cells yet.
 */
template <std::size_t COUNT>
void AddRows(std::vector<StatisticsRow> &rows, const std::array<std::string_view, COUNT> &names) {
    for (const std::string_view name : names) {
        rows.push_back({.Name = std::string(name), .Cells = {}});
    }
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
    AddRows(rows, MEASURED_ROWS);
    if (cursors) {
        AddRows(rows, CURSOR_ROWS);
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

/**
 * @brief   Reads the Bode measurements of every trace over the whole sweep, a row per quantity and a column per trace.
 * @param[in] frequencies  Swept frequencies.
 * @param[in] traces       Magnitude and phase of each trace, a column each in this order.
 * @param[in] cursors      The cursors when shown, which add the gain and phase of each trace at both and their
 *                         differences.
 * @return  The rows Peak, at, -3 dB low, -3 dB high, 0 dB at and Phase margin, then the six cursor rows with the
 *          cursors; a trace the sweep cannot measure reads "-" in the first six.
 */
std::vector<StatisticsRow> BuildBodeStatistics(const std::span<const double> frequencies,
                                               const std::vector<BodeTrace> &traces,
                                               const std::optional<PlotCursors> &cursors) {
    std::vector<StatisticsRow> rows;
    AddRows(rows, BODE_ROWS);
    if (cursors) {
        AddRows(rows, BODE_CURSOR_ROWS);
    }
    for (const BodeTrace &trace : traces) {
        std::size_t row = 0;
        const auto add = [&rows, &row](StatisticsCell cell) { rows[row++].Cells.push_back(std::move(cell)); };
        const std::optional<BodeStatistics> statistics =
            frequencies.empty() ? std::nullopt
                                : ComputeBodeStatistics(frequencies, trace.MagnitudesDecibels, trace.PhasesDegrees,
                                                        frequencies.front(), frequencies.back());
        if (statistics) {
            add(FixedCell(statistics->PeakGain, " dB"));
            add(PlainCell(statistics->PeakFrequency, "Hz"));
            add(PlainCell(statistics->LowerCutoff, "Hz"));
            add(PlainCell(statistics->UpperCutoff, "Hz"));
            add(PlainCell(statistics->UnityGainFrequency, "Hz"));
            add(FixedCell(statistics->PhaseMargin, " deg"));
        } else {
            while (row < BODE_ROWS.size()) {
                add(PlainCell(std::nullopt, ""));
            }
        }
        if (cursors) {
            const double gain_a = InterpolateAt(frequencies, trace.MagnitudesDecibels, cursors->A, true);
            const double gain_b = InterpolateAt(frequencies, trace.MagnitudesDecibels, cursors->B, true);
            const double phase_a = InterpolateAt(frequencies, trace.PhasesDegrees, cursors->A, true);
            const double phase_b = InterpolateAt(frequencies, trace.PhasesDegrees, cursors->B, true);
            add(FixedCell(gain_a, " dB"));
            add(FixedCell(gain_b, " dB"));
            add(FixedCell(gain_b - gain_a, " dB"));
            add(FixedCell(phase_a, " deg"));
            add(FixedCell(phase_b, " deg"));
            add(FixedCell(phase_b - phase_a, " deg"));
        }
    }
    return rows;
}

/**
 * @brief   Reads the curves of a DC sweep over a span, a row per quantity and a column per curve.
 * @param[in] swept       Swept values.
 * @param[in] swept_unit  Unit of the swept values, such as "V", which the slope is written over.
 * @param[in] traces      Curves to read, a column each in this order.
 * @param[in] span        Span of the swept axis the extremes and mean cover.
 * @param[in] cursors     The cursors when shown, which add the value of each curve at both, their difference and the
 *                        slope between them.
 * @return  With the cursors, the rows At A, At B, B-A and Slope, and Resistance when a current is swept over a
 *          voltage; then Min, Max and Mean. A current over a swept voltage reads its slope as a resistance too, "open"
 *          when it carries nothing; other curves read "-" there.
 */
std::vector<StatisticsRow> BuildSweepStatistics(const std::span<const double> swept, const std::string_view swept_unit,
                                                const std::vector<SweptTrace> &traces, const PlotSpan span,
                                                const std::optional<PlotCursors> &cursors) {
    const auto reads_resistance = [swept_unit](const SweptTrace &trace) {
        return trace.Unit == "A" && swept_unit == "V";
    };
    const bool has_resistance = cursors && std::ranges::any_of(traces, reads_resistance);
    std::vector<StatisticsRow> rows;
    if (cursors) {
        AddRows(rows, SWEEP_CURSOR_ROWS);
        if (has_resistance) {
            rows.push_back({.Name = "Resistance", .Cells = {}});
        }
    }
    AddRows(rows, SWEEP_ROWS);
    for (const SweptTrace &trace : traces) {
        std::size_t row = 0;
        const auto add = [&rows, &row](StatisticsCell cell) { rows[row++].Cells.push_back(std::move(cell)); };
        const std::optional<SweepStatistics> statistics =
            ComputeSweepStatistics(swept, trace.Values, span.From, span.To);
        // The size of the curve in the span tells residue from real values in every reading
        const double scale = statistics ? std::max(std::abs(statistics->Maximum), std::abs(statistics->Minimum)) : 0.0;
        if (cursors) {
            const double at_a = InterpolateAt(swept, trace.Values, cursors->A, false);
            const double at_b = InterpolateAt(swept, trace.Values, cursors->B, false);
            add(MeasureCell(at_a, scale, trace.Unit));
            add(MeasureCell(at_b, scale, trace.Unit));
            add(MeasureCell(at_b - at_a, scale, trace.Unit));
            const std::optional<double> slope = ComputeSlope(swept, trace.Values, cursors->A, cursors->B);
            add(PlainCell(slope, std::format("{}/{}", trace.Unit, swept_unit)));
            if (has_resistance) {
                if (!reads_resistance(trace) || !slope) {
                    add(PlainCell(std::nullopt, ""));
                } else {
                    // A zero slope is an open path, shown as such rather than as an infinite value
                    const std::optional<double> resistance = ResistanceFromConductance(*slope);
                    const bool is_open = !resistance || std::abs(*resistance) >= OPEN_RESISTANCE;
                    add(is_open ? StatisticsCell{.Text = "open", .Tooltip = {}} : PlainCell(resistance, "Ohm"));
                }
            }
        }
        if (statistics) {
            add(MeasureCell(statistics->Minimum, scale, trace.Unit));
            add(MeasureCell(statistics->Maximum, scale, trace.Unit));
            add(MeasureCell(statistics->Mean, scale, trace.Unit));
        } else {
            while (row < rows.size()) {
                add(PlainCell(std::nullopt, ""));
            }
        }
    }
    return rows;
}

} // namespace GUI
