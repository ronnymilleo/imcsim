/**
 * @file    check_noise.h
 * @brief   Checks the settings of a noise analysis before it runs.
 */

#ifndef IMCSIM_CHECK_NOISE_H
#define IMCSIM_CHECK_NOISE_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckNoiseSettings(const Circuit &circuit, const NoiseSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_NOISE_H
