/**
 * @file    check_s_parameters.h
 * @brief   Checks the ports and the settings of an S-parameter analysis before it runs.
 */

#ifndef IMCSIM_CHECK_S_PARAMETERS_H
#define IMCSIM_CHECK_S_PARAMETERS_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <cstddef>
#include <optional>
#include <string>

namespace Core {

/// Fewest ports an S-parameter analysis runs with, since ngspice refuses fewer.
constexpr std::size_t MIN_S_PARAMETER_PORTS = 2;

std::optional<std::string> CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_S_PARAMETERS_H
