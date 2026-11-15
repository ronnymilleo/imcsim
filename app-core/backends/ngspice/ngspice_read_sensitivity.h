/**
 * @file    ngspice_read_sensitivity.h
 * @brief   Reads the result of a sensitivity analysis from the plot ngspice leaves.
 */

#ifndef IMCSIM_NGSPICE_READ_SENSITIVITY_H
#define IMCSIM_NGSPICE_READ_SENSITIVITY_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <string>

namespace Core {

std::expected<Sensitivity, std::string> ReadSensitivity(const Circuit &circuit, const SensitivitySettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READ_SENSITIVITY_H
