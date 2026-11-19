/**
 * @file    harmonic_balance_samples.cpp
 * @brief   The time samples per period a harmonic balance may be set to, which stay above twice its harmonics.
 */

#include "analysis/harmonic_balance_samples.h"

#include "simulation/analysis_settings.h"

namespace GUI {

/**
 * @brief   Raises a sample count set by hand above twice the harmonics, the Nyquist limit, so a harmonic never aliases
 *          onto another one.
 * @param[in] samples    Samples per period, or 0 for a count picked from the harmonics.
 * @param[in] harmonics  Harmonics above DC.
 * @return  The count as it is when it is 0 or already above the limit; otherwise the smallest power of two from
 *          MIN_EXPLICIT_SAMPLES that is above it, up to Core::MAX_HARMONIC_BALANCE_SAMPLES.
 */
int NormalizeSampleCount(const int samples, const int harmonics) {
    if (samples == 0 || samples > 2 * harmonics) {
        return samples;
    }
    int next_power = MIN_EXPLICIT_SAMPLES;
    while (next_power <= 2 * harmonics && next_power < Core::MAX_HARMONIC_BALANCE_SAMPLES) {
        next_power *= 2;
    }
    return next_power;
}

/**
 * @brief   Lists the sample counts that can be set by hand for a number of harmonics.
 * @param[in] harmonics  Harmonics above DC.
 * @return  The powers of two from MIN_EXPLICIT_SAMPLES to Core::MAX_HARMONIC_BALANCE_SAMPLES that are above twice the
 *          harmonics, smallest first; empty when none is.
 */
std::vector<int> ListSampleCounts(const int harmonics) {
    std::vector<int> counts;
    for (int candidate = MIN_EXPLICIT_SAMPLES; candidate <= Core::MAX_HARMONIC_BALANCE_SAMPLES; candidate *= 2) {
        if (candidate > 2 * harmonics) {
            counts.push_back(candidate);
        }
    }
    return counts;
}

} // namespace GUI
