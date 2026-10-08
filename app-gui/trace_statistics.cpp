/**
 * @file    trace_statistics.cpp
 * @brief   Oscilloscope and Bode measurements of a trace over a span of its X axis.
 */

#include "trace_statistics.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace GUI {

namespace {

// Fraction of the peak-to-peak value the trace must swing back below its mean before another rising crossing
// counts, as the trigger hysteresis of an oscilloscope, so noise around the mean adds no cycles
constexpr double FrequencyHysteresis = 0.1;
constexpr double CutoffDrop = 3.0;

/**
 * @struct  Sample
 * @brief   One point of a trace.
 */
struct Sample {
    double X;
    double Y;
};

double Lerp(const Sample first, const Sample second, const double x) {
    if (second.X == first.X) {
        return first.Y;
    }
    return first.Y + (second.Y - first.Y) * (x - first.X) / (second.X - first.X);
}

// X where the segment from first to second reaches level, which must lie between their Y values
double CrossingX(const Sample first, const Sample second, const double level) {
    if (second.Y == first.Y) {
        return first.X;
    }
    return first.X + (second.X - first.X) * (level - first.Y) / (second.Y - first.Y);
}

// The trace between from and to, with points interpolated at both ends, so measurements cover exactly the span.
// xs must rise; on a logarithmic axis the X of every sample is its decade, so interpolation follows the plot
std::vector<Sample> ClipSamples(const std::span<const double> xs, const std::span<const double> ys, double from,
                                double to, const bool logarithmic) {
    std::vector<Sample> all;
    const std::size_t count = std::min(xs.size(), ys.size());
    for (std::size_t index = 0; index < count; ++index) {
        all.push_back({logarithmic ? std::log10(xs[index]) : xs[index], ys[index]});
    }
    if (all.empty()) {
        return {};
    }
    if (logarithmic) {
        from = std::log10(from);
        to = std::log10(to);
    }
    if (from > to) {
        std::swap(from, to);
    }
    from = std::max(from, all.front().X);
    to = std::min(to, all.back().X);
    if (!(from < to)) {
        return {};
    }

    std::vector<Sample> clipped;
    for (std::size_t index = 0; index < all.size(); ++index) {
        const Sample sample = all[index];
        if (index > 0 && all[index - 1].X < from && sample.X > from) {
            clipped.push_back({from, Lerp(all[index - 1], sample, from)});
        }
        if (sample.X >= from && sample.X <= to) {
            clipped.push_back(sample);
        }
        if (index > 0 && all[index - 1].X < to && sample.X > to) {
            clipped.push_back({to, Lerp(all[index - 1], sample, to)});
        }
    }
    return clipped;
}

// Rising crossings of the mean, one per cycle, averaged over every whole cycle in the span
std::optional<double> MeasureFrequency(const std::vector<Sample> &samples, const TraceStatistics &statistics) {
    if (statistics.PeakToPeak <= 0.0) {
        return std::nullopt;
    }
    const double level = statistics.Mean;
    const double rearm_level = level - FrequencyHysteresis * statistics.PeakToPeak;
    bool armed = false;
    std::optional<double> first_crossing;
    double last_crossing = 0.0;
    int cycles = 0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const Sample previous = samples[index - 1];
        const Sample current = samples[index];
        if (previous.Y <= rearm_level) {
            armed = true;
        }
        if (armed && previous.Y < level && current.Y >= level) {
            const double crossing = CrossingX(previous, current, level);
            if (first_crossing) {
                ++cycles;
            } else {
                first_crossing = crossing;
            }
            last_crossing = crossing;
            armed = false;
        }
    }
    if (cycles == 0 || last_crossing <= *first_crossing) {
        return std::nullopt;
    }
    return static_cast<double>(cycles) / (last_crossing - *first_crossing);
}

// First X, walking from start in steps of direction, where the trace falls through level; samples are in
// decades, so the result is converted back to a frequency
std::optional<double> FindFallingFrequency(const std::vector<Sample> &samples, const std::size_t start,
                                           const int direction, const double level) {
    for (std::size_t index = start; index < samples.size();) {
        const std::size_t next = index + static_cast<std::size_t>(direction);
        if (next >= samples.size()) {
            break;
        }
        if (samples[index].Y > level && samples[next].Y <= level) {
            return std::pow(10.0, CrossingX(samples[index], samples[next], level));
        }
        index = next;
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Reads a trace at any X, between its samples, as a cursor does.
 * @param[in] xs           X values of the samples, rising.
 * @param[in] ys           Y values of the samples.
 * @param[in] x            Where to read.
 * @param[in] logarithmic  Interpolates in decades, as a logarithmic axis draws the trace; X values must then be
 *                         positive.
 * @return  The interpolated value; the first or last sample outside the trace, and 0 for an empty trace.
 */
double InterpolateAt(const std::span<const double> xs, const std::span<const double> ys, const double x,
                     const bool logarithmic) {
    const std::size_t count = std::min(xs.size(), ys.size());
    if (count == 0) {
        return 0.0;
    }
    if (x <= xs[0]) {
        return ys[0];
    }
    if (x >= xs[count - 1]) {
        return ys[count - 1];
    }
    const auto upper = std::upper_bound(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(count), x);
    const auto index = static_cast<std::size_t>(upper - xs.begin());
    const auto position = [logarithmic](const double value) { return logarithmic ? std::log10(value) : value; };
    return Lerp({position(xs[index - 1]), ys[index - 1]}, {position(xs[index]), ys[index]}, position(x));
}

/**
 * @brief   Measures a trace between two X values, as an oscilloscope measures what is on screen.
 * @param[in] xs    X values of the samples, rising, such as the times of a transient.
 * @param[in] ys    Y values of the samples.
 * @param[in] from  One end of the span; the ends may come in any order and are clipped to the trace.
 * @param[in] to    The other end.
 * @return  The statistics, or no value when the span holds no length of the trace.
 * @note    The frequency counts the rising crossings of the mean, so it suits periodic signals; a trace that
 *          swings once, such as a charging capacitor, has none.
 */
std::optional<TraceStatistics> ComputeTraceStatistics(const std::span<const double> xs,
                                                      const std::span<const double> ys, const double from,
                                                      const double to) {
    const std::vector<Sample> samples = ClipSamples(xs, ys, from, to, false);
    if (samples.size() < 2) {
        return std::nullopt;
    }
    TraceStatistics statistics;
    const auto [lowest, highest] = std::ranges::minmax(samples, {}, &Sample::Y);
    statistics.Minimum = lowest.Y;
    statistics.Maximum = highest.Y;
    statistics.PeakToPeak = highest.Y - lowest.Y;

    // Exact integrals of the straight segments between samples
    double area = 0.0;
    double square_area = 0.0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const Sample first = samples[index - 1];
        const Sample second = samples[index];
        const double width = second.X - first.X;
        area += (first.Y + second.Y) / 2.0 * width;
        square_area += (first.Y * first.Y + first.Y * second.Y + second.Y * second.Y) / 3.0 * width;
    }
    const double duration = samples.back().X - samples.front().X;
    statistics.Mean = area / duration;
    statistics.RMS = std::sqrt(std::max(square_area / duration, 0.0));
    statistics.Frequency = MeasureFrequency(samples, statistics);
    return statistics;
}

/**
 * @brief   Measures a frequency response between two frequencies.
 * @param[in] frequencies  Frequencies of the samples, rising and positive.
 * @param[in] magnitudes   Gain at each frequency, in dB.
 * @param[in] phases       Phase at each frequency, in degrees between -180 and 180.
 * @param[in] from         One end of the span; the ends may come in any order and are clipped to the sweep.
 * @param[in] to           The other end.
 * @return  The statistics, or no value when the span holds no length of the sweep.
 * @note    Crossings are interpolated in decades, as the plot draws them. The phase margin assumes the response
 *          is a loop gain; for a filter or an amplifier it is only the phase at 0 dB, shifted by 180 degrees.
 */
std::optional<BodeStatistics> ComputeBodeStatistics(const std::span<const double> frequencies,
                                                    const std::span<const double> magnitudes,
                                                    const std::span<const double> phases, const double from,
                                                    const double to) {
    const std::vector<Sample> samples = ClipSamples(frequencies, magnitudes, from, to, true);
    if (samples.size() < 2) {
        return std::nullopt;
    }
    BodeStatistics statistics;
    const auto peak = std::ranges::max_element(samples, {}, &Sample::Y);
    const auto peak_index = static_cast<std::size_t>(peak - samples.begin());
    statistics.PeakGain = peak->Y;
    statistics.PeakFrequency = std::pow(10.0, peak->X);
    statistics.UpperCutoff = FindFallingFrequency(samples, peak_index, 1, peak->Y - CutoffDrop);
    statistics.LowerCutoff = FindFallingFrequency(samples, peak_index, -1, peak->Y - CutoffDrop);
    statistics.UnityGainFrequency = FindFallingFrequency(samples, 0, 1, 0.0);
    if (statistics.UnityGainFrequency) {
        statistics.PhaseMargin = 180.0 + InterpolateAt(frequencies, phases, *statistics.UnityGainFrequency, true);
    }
    return statistics;
}

} // namespace GUI
