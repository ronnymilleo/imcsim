/**
 * @file    ngspice_read_pole_zero.h
 * @brief   Reads the poles and zeros from the plot ngspice leaves.
 */

#ifndef IMCSIM_NGSPICE_READ_POLE_ZERO_H
#define IMCSIM_NGSPICE_READ_POLE_ZERO_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<PoleZero, std::string> ReadPoleZero(const Circuit &circuit, const PoleZeroSettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_POLE_ZERO_H
