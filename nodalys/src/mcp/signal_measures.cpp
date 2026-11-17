/**
 * @file    signal_measures.cpp
 * @brief   The few figures the compact answers give for a waveform or a frequency response.
 */

#include "mcp/signal_measures.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

/// Decibels below the peak that define the cutoff frequencies.
constexpr double CUTOFF_DROP_DECIBELS = 3.0;

/// The point between two samples where a line through them reaches a level, as a fraction of the way from the first.
double FractionToLevel(const double from, const double to, const double level) {
    return (level - from) / (to - from);
}

/**
 * @brief   Interpolates between two frequency points, on a logarithmic frequency axis when both are positive.
 * @details A sweep is spaced by decade or octave, where the gain is close to a line against log frequency, so the
 *          crossing is found more exactly than on a linear axis.
 */
double InterpolateFrequency(const double low, const double high, const double fraction) {
    if (low > 0.0 && high > 0.0) {
        return std::pow(10.0, std::log10(low) + fraction * (std::log10(high) - std::log10(low)));
    }
    return low + fraction * (high - low);
}

/**
 * @brief   Finds where the gain first crosses a level between two sample indexes, in one direction.
 * @return  The index of the sample before the crossing and the fraction towards the next one, or none.
 */
std::optional<std::pair<std::size_t, double>> FindCrossing(std::span<const double> gains, const std::size_t first,
                                                           const std::size_t last, const double level,
                                                           const bool is_falling) {
    for (std::size_t index = first; index + 1 <= last && index + 1 < gains.size(); ++index) {
        const double before = gains[index];
        const double after = gains[index + 1];
        const bool is_crossing = is_falling ? (before > level && after <= level) : (before < level && after >= level);
        if (is_crossing) {
            return std::pair{index, FractionToLevel(before, after, level)};
        }
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Measures a waveform.
 * @param[in] times   Sample times, ascending.
 * @param[in] values  Value at each time.
 * @return  The figures, or none when there are no samples or the two lists differ in length.
 */
std::optional<WaveformFigures> MeasureWaveform(const std::span<const double> times,
                                               const std::span<const double> values) {
    if (values.empty() || times.size() != values.size()) {
        return std::nullopt;
    }
    WaveformFigures figures;
    figures.Minimum = *std::ranges::min_element(values);
    figures.Maximum = *std::ranges::max_element(values);
    figures.Final = values.back();

    double integral = 0.0;
    double squared_integral = 0.0;
    for (std::size_t index = 0; index + 1 < values.size(); ++index) {
        const double step = times[index + 1] - times[index];
        integral += 0.5 * (values[index] + values[index + 1]) * step;
        // The trapezoid of the square is exact for a signal that varies linearly between samples
        const double square_before = values[index] * values[index];
        const double square_after = values[index + 1] * values[index + 1];
        const double cross = values[index] * values[index + 1];
        squared_integral += (square_before + cross + square_after) / 3.0 * step;
    }
    const double span = times.back() - times.front();
    if (span > 0.0) {
        figures.Mean = integral / span;
        figures.Rms = std::sqrt(squared_integral / span);
    } else {
        // A single point, such as a run that stopped at once, has the value of that point
        figures.Mean = values.front();
        figures.Rms = std::abs(values.front());
    }
    return figures;
}

/**
 * @brief   Measures a frequency response.
 * @param[in] frequencies     Frequencies in hertz, ascending.
 * @param[in] gains_decibels  Gain at each frequency.
 * @param[in] phases_degrees  Phase at each frequency, unwrapped.
 * @return  The figures, or none when there are no points or the lists differ in length.
 */
std::optional<ResponseFigures> MeasureResponse(const std::span<const double> frequencies,
                                               const std::span<const double> gains_decibels,
                                               const std::span<const double> phases_degrees) {
    if (gains_decibels.empty() || frequencies.size() != gains_decibels.size() ||
        phases_degrees.size() != gains_decibels.size()) {
        return std::nullopt;
    }
    const std::size_t peak_index =
        static_cast<std::size_t>(std::ranges::max_element(gains_decibels) - gains_decibels.begin());
    ResponseFigures figures;
    figures.PeakGainDecibels = gains_decibels[peak_index];
    figures.PeakFrequency = frequencies[peak_index];

    const double cutoff_level = figures.PeakGainDecibels - CUTOFF_DROP_DECIBELS;
    if (const auto crossing = FindCrossing(gains_decibels, 0, peak_index, cutoff_level, false)) {
        const auto [index, fraction] = *crossing;
        figures.LowerCutoffFrequency = InterpolateFrequency(frequencies[index], frequencies[index + 1], fraction);
    }
    if (const auto crossing = FindCrossing(gains_decibels, peak_index, gains_decibels.size() - 1, cutoff_level, true)) {
        const auto [index, fraction] = *crossing;
        figures.UpperCutoffFrequency = InterpolateFrequency(frequencies[index], frequencies[index + 1], fraction);
    }
    if (const auto crossing = FindCrossing(gains_decibels, 0, gains_decibels.size() - 1, 0.0, true)) {
        const auto [index, fraction] = *crossing;
        figures.UnityGainFrequency = InterpolateFrequency(frequencies[index], frequencies[index + 1], fraction);
        figures.PhaseAtUnityGainDegrees =
            phases_degrees[index] + fraction * (phases_degrees[index + 1] - phases_degrees[index]);
    }
    return figures;
}

} // namespace Nodalys
