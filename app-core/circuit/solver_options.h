/**
 * @file    solver_options.h
 * @brief   The numerical options of a simulation: tolerances, integration method and nominal temperature.
 */

#ifndef IMCSIM_SOLVER_OPTIONS_H
#define IMCSIM_SOLVER_OPTIONS_H

namespace Core {

/**
 * @enum    IntegrationMethod
 * @brief   The formula a transient uses to step capacitors and inductors in time.
 */
enum class IntegrationMethod {
    /// The trapezoidal rule, or Gear when the circuit has a fuse, whose cut inductor makes the trapezoidal rule ring.
    DEFAULT,
    TRAPEZOIDAL,
    GEAR
};

/// Relative tolerance both engines start from.
constexpr double DEFAULT_RELATIVE_TOLERANCE = 1e-3;
/// Absolute current tolerance, in amperes, both engines start from.
constexpr double DEFAULT_CURRENT_TOLERANCE_AMPERES = 1e-12;
/// Absolute voltage tolerance, in volts, both engines start from.
constexpr double DEFAULT_VOLTAGE_TOLERANCE_VOLTS = 1e-6;
/// Temperature, in degrees Celsius, at which model parameters are given and a run simulates unless it is stepped.
constexpr double DEFAULT_NOMINAL_TEMPERATURE_CELSIUS = 27.0;

/**
 * @struct  SolverOptions
 * @brief   Options of the solver that both engines honour: the tolerances of the Newton iteration and of the step
 *          control, the integration method and the nominal temperature (SPICE reltol, abstol, vntol, method, tnom).
 * @details The defaults are the ones of SPICE, so a circuit that never touches them simulates as it always did.
 */
struct SolverOptions {
    double RelativeTolerance = DEFAULT_RELATIVE_TOLERANCE;
    double CurrentToleranceAmperes = DEFAULT_CURRENT_TOLERANCE_AMPERES;
    double VoltageToleranceVolts = DEFAULT_VOLTAGE_TOLERANCE_VOLTS;
    IntegrationMethod Method = IntegrationMethod::DEFAULT;
    /// The temperature the model parameters are given at; a run at another temperature scales them from it.
    double NominalTemperatureCelsius = DEFAULT_NOMINAL_TEMPERATURE_CELSIUS;

    bool operator==(const SolverOptions &) const = default;
};

} // namespace Core

#endif // IMCSIM_SOLVER_OPTIONS_H
