/**
 * @file    check_ac_sweep.cpp
 * @brief   Checks the settings of an AC sweep before it runs.
 */

#include "simulation/check_ac_sweep.h"

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/check_frequency_range.h"
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Checks the frequency range of an AC sweep and that the circuit has an AC source to drive it.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Frequency range and points per decade.
 * @return  The reason the sweep cannot run; no value when it can.
 */
std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (!circuit.HasACSource()) {
        return "An AC sweep needs an AC source; select a voltage or current source and set it to AC";
    }
    return std::nullopt;
}

} // namespace Core
