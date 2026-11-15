/**
 * @file    check_frequency_range.h
 * @brief   Checks the frequency range that the AC sweep, the noise and the S-parameter analyses share.
 */

#ifndef IMCSIM_CHECK_FREQUENCY_RANGE_H
#define IMCSIM_CHECK_FREQUENCY_RANGE_H

#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckFrequencyRange(double start_frequency, double stop_frequency, int points_per_decade);

} // namespace Core

#endif // IMCSIM_CHECK_FREQUENCY_RANGE_H
