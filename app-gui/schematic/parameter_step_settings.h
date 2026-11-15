/**
 * @file    parameter_step_settings.h
 * @brief   Settings of a stepped parameter and the values it takes, saved with the schematic.
 */

#ifndef IMCSIM_PARAMETER_STEP_SETTINGS_H
#define IMCSIM_PARAMETER_STEP_SETTINGS_H

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  ParameterStepSettings
 * @brief   The parameter a transient, AC sweep or DC sweep steps through, one run per value, and its values.
 * @details Part names a resistor, capacitor or inductor; empty, the temperature steps. The values go from Start to
 *          Stop in steps of Step, or are typed as a list such as "1k, 2.2k, 4.7k". A part that left the circuit fails
 *          the run, so the stored name is never rewritten behind the user.
 */
struct ParameterStepSettings {
    bool Enabled = false;
    std::string Part;
    bool UsesList = false;
    double Start = 0.0;
    double Stop = 100.0;
    double Step = 25.0;
    std::string List;

    bool operator==(const ParameterStepSettings &) const = default;
};

constexpr std::size_t MAX_PARAMETER_STEPS = 50;

std::expected<std::vector<double>, std::string> ListStepValues(const ParameterStepSettings &settings,
                                                               const std::string &unit);
std::string GetParameterLabel(const ParameterStepSettings &settings);

} // namespace GUI

#endif // IMCSIM_PARAMETER_STEP_SETTINGS_H
