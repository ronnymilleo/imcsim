/**
 * @file    check_ac_sweep.h
 * @brief   Checks the settings of an AC sweep before it runs.
 */

#ifndef IMCSIM_CHECK_AC_SWEEP_H
#define IMCSIM_CHECK_AC_SWEEP_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_AC_SWEEP_H
