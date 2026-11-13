/**
 * @file    ngspice_read_s_parameters.h
 * @brief   Reads the scattering matrix of an S-parameter analysis from the current plot of ngspice.
 */

#ifndef IMCSIM_NGSPICE_READ_S_PARAMETERS_H
#define IMCSIM_NGSPICE_READ_S_PARAMETERS_H

#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<SParameters, std::string> ReadSParameters(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_S_PARAMETERS_H
