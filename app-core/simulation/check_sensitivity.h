/**
 * @file    check_sensitivity.h
 * @brief   Checks the settings of a sensitivity analysis before it runs.
 */

#ifndef IMCSIM_CHECK_SENSITIVITY_H
#define IMCSIM_CHECK_SENSITIVITY_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckSensitivitySettings(const Circuit &circuit, const SensitivitySettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_SENSITIVITY_H
