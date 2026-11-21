/**
 * @file    harmonic_balance_samples.h
 * @brief   The time samples per period a harmonic balance may be set to, which stay above twice its harmonics.
 */

#ifndef IMCSIM_HARMONIC_BALANCE_SAMPLES_H
#define IMCSIM_HARMONIC_BALANCE_SAMPLES_H

#include <vector>

namespace GUI {

/// Fewest samples per period that can be set by hand; 0 still picks a count from the harmonics.
constexpr int MIN_EXPLICIT_SAMPLES = 32;

// Sample counts
int NormalizeSampleCount(int samples, int harmonics);
std::vector<int> ListSampleCounts(int harmonics);

} // namespace GUI

#endif // IMCSIM_HARMONIC_BALANCE_SAMPLES_H
