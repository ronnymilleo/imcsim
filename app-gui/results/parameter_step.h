/**
 * @file    parameter_step.h
 * @brief   The results of an analysis run once per value of a stepped parameter, and how they are labeled and joined.
 */

#ifndef IMCSIM_PARAMETER_STEP_H
#define IMCSIM_PARAMETER_STEP_H

#include "simulation/simulator.h"
#include <cstddef>
#include <string>
#include <vector>

namespace GUI {

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

/// Name of the parameter of a Monte Carlo family, whose values are run numbers: 0 is the nominal run.
constexpr const char *MONTE_CARLO_PARAMETER = "Run";

std::string FormatStepValue(const std::string &parameter, double value, const std::string &unit);
Core::DCSweep JoinDCSweepSteps(StepFamily<Core::DCSweep> family);
Core::DCSweep JoinOperatingPointSteps(const StepFamily<Core::OperatingPoint> &family);

} // namespace GUI

#endif // IMCSIM_PARAMETER_STEP_H
