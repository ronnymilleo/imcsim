/**
 * @file    ngspice_read_noise.h
 * @brief   Reads the result of a noise analysis from the two plots ngspice leaves.
 */

#ifndef IMCSIM_NGSPICE_READ_NOISE_H
#define IMCSIM_NGSPICE_READ_NOISE_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<Noise, std::string> ReadNoise(const Circuit &circuit, const NoiseSettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_NOISE_H
