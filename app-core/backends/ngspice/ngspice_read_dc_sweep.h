/**
 * @file    ngspice_read_dc_sweep.h
 * @brief   Reads the result of a DC sweep from the current plot of ngspice.
 */

#ifndef IMCSIM_NGSPICE_READ_DC_SWEEP_H
#define IMCSIM_NGSPICE_READ_DC_SWEEP_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<DCSweep, std::string> ReadDCSweep(const Circuit &circuit, const DCSweepSettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_DC_SWEEP_H
