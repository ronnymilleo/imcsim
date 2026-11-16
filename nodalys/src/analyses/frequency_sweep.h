/**
 * @file    frequency_sweep.h
 * @brief   The frequency grid of the small-signal sweeps (AC, noise, AC sensitivity and S-parameters).
 */

#ifndef NODALYS_FREQUENCY_SWEEP_H
#define NODALYS_FREQUENCY_SWEEP_H

#include "nodalys/analyses.h"
#include <vector>

namespace Nodalys {

// Grid
std::vector<double> MakeFrequencySweep(FrequencySweepType sweep_type, double start_frequency, double stop_frequency,
                                       int points);

} // namespace Nodalys

#endif // NODALYS_FREQUENCY_SWEEP_H
