/**
 * @file    trace_statistics.h
 * @brief   Oscilloscope and Bode measurements of a trace over a span of its X axis.
 */

#ifndef IMCSIM_TRACE_STATISTICS_H
#define IMCSIM_TRACE_STATISTICS_H

#include <optional>
#include <span>

namespace GUI {

/**
 * @struct  TraceStatistics
 * @brief   What an oscilloscope measures on a trace: extremes, mean, RMS and frequency.
 * @details Mean and RMS are weighted by time, since ngspice spaces its samples unevenly.
 */
struct TraceStatistics {
    double Minimum = 0.0;
    double Maximum = 0.0;
    double PeakToPeak = 0.0;
    double Mean = 0.0;
    double RMS = 0.0;
    // No value when the trace does not repeat at least once within the span
    std::optional<double> Frequency;
};

/**
 * @struct  BodeStatistics
 * @brief   What a Bode plot shows of a response: its peak, its -3 dB band and its unity gain point.
 */
struct BodeStatistics {
    double PeakGain = 0.0;
    double PeakFrequency = 0.0;
    // Where the gain falls 3 dB below the peak, below and above it; no value when it does not within the span
    std::optional<double> LowerCutoff;
    std::optional<double> UpperCutoff;
    // Where the gain first falls through 0 dB, and 180 degrees plus the phase there, as for a loop gain
    std::optional<double> UnityGainFrequency;
    std::optional<double> PhaseMargin;
};

double InterpolateAt(std::span<const double> xs, std::span<const double> ys, double x, bool logarithmic);
std::optional<TraceStatistics> ComputeTraceStatistics(std::span<const double> xs, std::span<const double> ys,
                                                      double from, double to);
std::optional<BodeStatistics> ComputeBodeStatistics(std::span<const double> frequencies,
                                                    std::span<const double> magnitudes, std::span<const double> phases,
                                                    double from, double to);

} // namespace GUI

#endif // IMCSIM_TRACE_STATISTICS_H
