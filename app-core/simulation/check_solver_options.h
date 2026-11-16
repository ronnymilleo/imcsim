/**
 * @file    check_solver_options.h
 * @brief   Checks the solver options of a circuit before any analysis runs on it.
 */

#ifndef IMCSIM_CHECK_SOLVER_OPTIONS_H
#define IMCSIM_CHECK_SOLVER_OPTIONS_H

#include "circuit/solver_options.h"
#include <optional>
#include <string>

namespace Core {

/// Smallest relative tolerance a run takes; tighter than this the solver chases rounding noise.
constexpr double MIN_RELATIVE_TOLERANCE = 1e-9;
/// Largest relative tolerance a run takes; looser than this the answer is no longer a solution.
constexpr double MAX_RELATIVE_TOLERANCE = 0.1;
/// Smallest absolute current tolerance, in amperes.
constexpr double MIN_CURRENT_TOLERANCE_AMPERES = 1e-18;
/// Largest absolute current tolerance, in amperes.
constexpr double MAX_CURRENT_TOLERANCE_AMPERES = 1e-6;
/// Smallest absolute voltage tolerance, in volts.
constexpr double MIN_VOLTAGE_TOLERANCE_VOLTS = 1e-12;
/// Largest absolute voltage tolerance, in volts.
constexpr double MAX_VOLTAGE_TOLERANCE_VOLTS = 1e-2;
/// Lowest nominal temperature, in degrees Celsius, which the models are given at.
constexpr double MIN_NOMINAL_TEMPERATURE_CELSIUS = -200.0;
/// Highest nominal temperature, in degrees Celsius.
constexpr double MAX_NOMINAL_TEMPERATURE_CELSIUS = 500.0;

std::optional<std::string> CheckSolverOptions(const SolverOptions &options);

} // namespace Core

#endif // IMCSIM_CHECK_SOLVER_OPTIONS_H
