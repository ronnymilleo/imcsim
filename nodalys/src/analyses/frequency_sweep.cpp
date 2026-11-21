/**
 * @file    frequency_sweep.cpp
 * @brief   The frequency grid of the small-signal sweeps (AC, noise, AC sensitivity and S-parameters).
 */

#include "analyses/frequency_sweep.h"

#include <algorithm>
#include <cmath>

namespace Nodalys {

namespace {

/// Relative margin within which a point counts as the stop frequency, so rounding in the power never drops it.
constexpr double STOP_TOLERANCE = 1e-9;

constexpr double DECADE_BASE = 10.0;
constexpr double OCTAVE_BASE = 2.0;

std::vector<double> MakeLinearSweep(const double start_frequency, const double stop_frequency, const int points) {
    if (points == 1 || start_frequency == stop_frequency) {
        return {start_frequency};
    }
    const double step = (stop_frequency - start_frequency) / static_cast<double>(points - 1);
    std::vector<double> frequencies;
    for (int index = 0; index < points; ++index) {
        frequencies.push_back(std::min(start_frequency + static_cast<double>(index) * step, stop_frequency));
    }
    return frequencies;
}

std::vector<double> MakeLogarithmicSweep(const double base,
                                         const double start_frequency,
                                         const double stop_frequency,
                                         const int points_per_step) {
    std::vector<double> frequencies;
    for (int index = 0;; ++index) {
        const double frequency =
            start_frequency * std::pow(base, static_cast<double>(index) / static_cast<double>(points_per_step));
        if (frequency > stop_frequency * (1.0 + STOP_TOLERANCE)) {
            break;
        }
        frequencies.push_back(std::min(frequency, stop_frequency));
        if (frequency >= stop_frequency * (1.0 - STOP_TOLERANCE)) {
            break;
        }
    }
    return frequencies;
}

} // namespace

/**
 * @brief   Tells whether a frequency range and its point count can make a sweep.
 * @param[in] start_frequency  First frequency in hertz.
 * @param[in] stop_frequency   Last frequency in hertz.
 * @param[in] points           Points per decade or octave, or in all of a linear sweep.
 * @return  False when the start is not above 0, the stop is below the start or there is no point; a value that is
 *          not a number passes, as the comparisons that refuse a range say nothing about it.
 */
bool IsValidSweepRange(const double start_frequency, const double stop_frequency, const int points) {
    return !(start_frequency <= 0.0 || stop_frequency < start_frequency || points <= 0);
}

/**
 * @brief   Lists the frequencies of a sweep, from the start up to the stop.
 * @details A logarithmic sweep multiplies the frequency by a fixed ratio and ends at the last point not above the
 *          stop, which is included only when the grid reaches it; a linear one spaces the points evenly and always
 *          ends on the stop. A sweep whose start equals its stop is one point.
 * @param[in] sweep_type       Spacing of the points.
 * @param[in] start_frequency  First frequency in hertz, above 0.
 * @param[in] stop_frequency   Last frequency in hertz, not below the start.
 * @param[in] points           Points per decade or per octave of a logarithmic sweep, or the points in all of a
 *                             linear one; at least 1.
 * @return  The frequencies in ascending order.
 * @note    The caller checks the range and the point count, each analysis in its own words.
 */
std::vector<double> MakeFrequencySweep(const FrequencySweepType sweep_type,
                                       const double start_frequency,
                                       const double stop_frequency,
                                       const int points) {
    switch (sweep_type) {
    case FrequencySweepType::LINEAR:
        return MakeLinearSweep(start_frequency, stop_frequency, points);
    case FrequencySweepType::OCTAVE:
        return MakeLogarithmicSweep(OCTAVE_BASE, start_frequency, stop_frequency, points);
    case FrequencySweepType::DECADE:
    default:
        return MakeLogarithmicSweep(DECADE_BASE, start_frequency, stop_frequency, points);
    }
}

} // namespace Nodalys
