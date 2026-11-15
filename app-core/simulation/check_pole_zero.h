/**
 * @file    check_pole_zero.h
 * @brief   Checks the settings of a pole-zero analysis before it runs.
 */

#ifndef IMCSIM_CHECK_POLE_ZERO_H
#define IMCSIM_CHECK_POLE_ZERO_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckPoleZeroSettings(const Circuit &circuit, const PoleZeroSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_POLE_ZERO_H
