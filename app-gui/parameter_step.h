/**
 * @file    parameter_step.h
 * @brief   Settings and values of a stepped parameter, and the results of an analysis run once per value.
 */

#ifndef IMCSIM_PARAMETER_STEP_H
#define IMCSIM_PARAMETER_STEP_H

#include "simulator.h"
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

/**
 * @struct  StepFamily
 * @brief   The results of an analysis run once per value of a stepped parameter, and the step the views show.
 * @details Parameter is the part name, or "T" for the temperature, and Unit the unit of its values, as
 *          FormatStepValue() writes them.
 */
template <typename Result> struct StepFamily {
    std::string Parameter;
    std::string Unit;
    std::vector<double> Values;
    std::vector<Result> Results;
    std::size_t Selected = 0;
};

constexpr std::size_t MaxParameterSteps = 50;

std::expected<std::vector<double>, std::string> ListStepValues(const ParameterStepSettings &settings,
                                                               const std::string &unit);
std::string GetParameterLabel(const ParameterStepSettings &settings);
std::string FormatStepValue(const std::string &parameter, double value, const std::string &unit);
Core::DCSweep JoinDCSweepSteps(StepFamily<Core::DCSweep> family);

} // namespace GUI

#endif // IMCSIM_PARAMETER_STEP_H
