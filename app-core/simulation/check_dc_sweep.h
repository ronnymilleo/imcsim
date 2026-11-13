/**
 * @file    check_dc_sweep.h
 * @brief   Checks the settings of a DC sweep before it runs.
 */

#ifndef IMCSIM_CHECK_DC_SWEEP_H
#define IMCSIM_CHECK_DC_SWEEP_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckDCSweepSettings(const Circuit &circuit, const DCSweepSettings &settings);

} // namespace Core

#endif // IMCSIM_CHECK_DC_SWEEP_H
