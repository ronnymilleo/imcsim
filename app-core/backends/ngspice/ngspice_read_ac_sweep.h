/**
 * @file    ngspice_read_ac_sweep.h
 * @brief   Reads the result of an AC sweep from the current plot of ngspice.
 */

#ifndef IMCSIM_NGSPICE_READ_AC_SWEEP_H
#define IMCSIM_NGSPICE_READ_AC_SWEEP_H

#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<ACSweep, std::string> ReadACSweep(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_AC_SWEEP_H
