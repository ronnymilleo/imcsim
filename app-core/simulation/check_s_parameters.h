/**
 * @file    check_s_parameters.h
 * @brief   Checks the ports and the settings of an S-parameter analysis before it runs.
 */

#ifndef IMCSIM_CHECK_S_PARAMETERS_H
#define IMCSIM_CHECK_S_PARAMETERS_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_backend.h"
#include <cstddef>
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckSParameterPortCount(std::size_t port_count, const SParameterLimits &limits);
std::optional<std::string> CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings,
                                                   const SParameterLimits &limits);

} // namespace Core

#endif // IMCSIM_CHECK_S_PARAMETERS_H
