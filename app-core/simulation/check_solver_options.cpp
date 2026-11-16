/**
 * @file    check_solver_options.cpp
 * @brief   Checks the solver options of a circuit before any analysis runs on it.
 */

#include "simulation/check_solver_options.h"

#include "spice/spice_value.h"
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace Core {

namespace {

/**
 * @brief   The problem with one option that has to lie in a range, or nothing when it does.
 */
std::optional<std::string> CheckRange(const std::string_view name, const std::string_view unit, const double value,
                                      const double minimum, const double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return std::format("The {} must be between {}{} and {}{}", name, FormatValue(minimum), unit,
                           FormatValue(maximum), unit);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks the tolerances and the nominal temperature of the solver.
 * @param[in] options  Options to check.
 * @return  The reason a run cannot use them, naming the option and its range; no value when they are fine.
 */
std::optional<std::string> CheckSolverOptions(const SolverOptions &options) {
    if (std::optional<std::string> problem = CheckRange("relative tolerance (reltol)", "", options.RelativeTolerance,
                                                        MIN_RELATIVE_TOLERANCE, MAX_RELATIVE_TOLERANCE)) {
        return problem;
    }
    if (std::optional<std::string> problem =
            CheckRange("current tolerance (abstol)", "A", options.CurrentToleranceAmperes,
                       MIN_CURRENT_TOLERANCE_AMPERES, MAX_CURRENT_TOLERANCE_AMPERES)) {
        return problem;
    }
    if (std::optional<std::string> problem = CheckRange("voltage tolerance (vntol)", "V", options.VoltageToleranceVolts,
                                                        MIN_VOLTAGE_TOLERANCE_VOLTS, MAX_VOLTAGE_TOLERANCE_VOLTS)) {
        return problem;
    }
    return CheckRange("nominal temperature (tnom)", " C", options.NominalTemperatureCelsius,
                      MIN_NOMINAL_TEMPERATURE_CELSIUS, MAX_NOMINAL_TEMPERATURE_CELSIUS);
}

} // namespace Core
