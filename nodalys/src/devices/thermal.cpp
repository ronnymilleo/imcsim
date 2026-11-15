/**
 * @file    thermal.cpp
 * @brief   The SPICE temperature laws the semiconductor devices of Nodalys share: the thermal voltage, the silicon
 *          band gap, the growth of a saturation current and the shift of a junction potential and capacitance.
 */

#include "devices/thermal.h"

#include <cmath>

namespace Nodalys {

namespace {

/// Constant of the junction potential law that makes it match the silicon band gap at the reference temperature.
constexpr double REFERENCE_BAND_GAP = 1.1150877;
/// Temperature coefficient of the junction capacitance law, per kelvin, as ngspice writes it.
constexpr double CAPACITANCE_TEMPERATURE_COEFFICIENT = 4e-4;

/// Potential shift $P(T)$ of the junction potential law at one temperature.
double GetPotentialShift(const double temperature_kelvin) {
    const double ratio = temperature_kelvin / REFERENCE_TEMPERATURE_KELVIN;
    const double thermal_voltage = BOLTZMANN_OVER_CHARGE * temperature_kelvin;
    return GetBandGap(temperature_kelvin) - REFERENCE_BAND_GAP * ratio - 3.0 * thermal_voltage * std::log(ratio);
}

/// Potential $V_{J0}$ that a junction potential given at the nominal temperature has at the reference temperature.
double GetReferencePotential(const ThermalConditions &thermal, const double potential_at_nominal) {
    const double nominal_ratio = thermal.NominalKelvin / REFERENCE_TEMPERATURE_KELVIN;
    return (potential_at_nominal - GetPotentialShift(thermal.NominalKelvin)) / nominal_ratio;
}

} // namespace

/**
 * @brief   Builds the conditions of a run from temperatures in degrees Celsius.
 * @param[in] temperature_celsius  Temperature of the run.
 * @param[in] nominal_celsius      Temperature the model parameters are given at (`TNOM`).
 * @return  The same temperatures in kelvin.
 */
ThermalConditions MakeThermalConditions(const double temperature_celsius, const double nominal_celsius) {
    return {.TemperatureKelvin = temperature_celsius + CELSIUS_TO_KELVIN,
            .NominalKelvin = nominal_celsius + CELSIUS_TO_KELVIN};
}

/**
 * @brief   Tells whether the run is at the nominal temperature, where every law returns the given parameter itself.
 * @param[in] thermal  Conditions of the run.
 * @return  True when the temperature equals the nominal one.
 */
bool IsNominal(const ThermalConditions &thermal) {
    return thermal.TemperatureKelvin == thermal.NominalKelvin;
}

/**
 * @brief   Returns the thermal voltage $kT/q$ of the run.
 * @param[in] thermal  Conditions of the run.
 * @return  Volts.
 */
double GetThermalVoltage(const ThermalConditions &thermal) {
    return BOLTZMANN_OVER_CHARGE * thermal.TemperatureKelvin;
}

/**
 * @brief   Returns $4kT$, the factor of the thermal noise current density $4kT/R$ of a conductance.
 * @param[in] thermal  Conditions of the run.
 * @return  Joules; ngspice evaluates every thermal noise source at the temperature of the circuit.
 */
double GetFourKt(const ThermalConditions &thermal) {
    return 4.0 * BOLTZMANN_CONSTANT * thermal.TemperatureKelvin;
}

/**
 * @brief   Returns the band gap of silicon at a temperature.
 * @param[in] temperature_kelvin  Temperature in kelvin.
 * @return  Electron-volts, from the Varshni law $E_g = 1.16 - 7.02\cdot10^{-4} T^2 / (T + 1108)$.
 */
double GetBandGap(const double temperature_kelvin) {
    return BAND_GAP_AT_ZERO_KELVIN -
           (BAND_GAP_SLOPE * temperature_kelvin * temperature_kelvin) / (temperature_kelvin + BAND_GAP_OFFSET_KELVIN);
}

/**
 * @brief   Returns the logarithm of the growth of a saturation current between the nominal and the run temperature.
 * @param[in] thermal                      Conditions of the run.
 * @param[in] energy_gap                   Band gap `EG` of the model, in electron-volts.
 * @param[in] saturation_current_exponent  Exponent `XTI` of the temperature ratio.
 * @return  The exponent of the growth factor; a diode or leakage current divides it by its emission coefficient
 *          before taking the exponential, and the factor is exactly 1 at the nominal temperature.
 */
double GetSaturationCurrentLogRatio(const ThermalConditions &thermal, const double energy_gap,
                                    const double saturation_current_exponent) {
    const double ratio = thermal.TemperatureKelvin / thermal.NominalKelvin;
    return (ratio - 1.0) * energy_gap / GetThermalVoltage(thermal) + saturation_current_exponent * std::log(ratio);
}

/**
 * @brief   Returns a junction potential at the run temperature.
 * @param[in] thermal               Conditions of the run.
 * @param[in] potential_at_nominal  Potential `VJ` (or `PHI`) given at the nominal temperature, in volts.
 * @return  Volts; the given potential itself at the nominal temperature.
 */
double GetJunctionPotential(const ThermalConditions &thermal, const double potential_at_nominal) {
    if (IsNominal(thermal)) {
        return potential_at_nominal;
    }
    const double reference_ratio = thermal.TemperatureKelvin / REFERENCE_TEMPERATURE_KELVIN;
    return reference_ratio * GetReferencePotential(thermal, potential_at_nominal) +
           GetPotentialShift(thermal.TemperatureKelvin);
}

/**
 * @brief   Returns a zero-bias junction capacitance at the run temperature.
 * @param[in] thermal                Conditions of the run.
 * @param[in] capacitance_at_nominal Capacitance `CJ0` given at the nominal temperature, in farads.
 * @param[in] potential_at_nominal   Potential `VJ` given at the nominal temperature, in volts.
 * @param[in] grading_coefficient    Grading coefficient `M` of the junction.
 * @return  Farads; the given capacitance itself at the nominal temperature.
 */
double GetJunctionCapacitance(const ThermalConditions &thermal, const double capacitance_at_nominal,
                              const double potential_at_nominal, const double grading_coefficient) {
    if (IsNominal(thermal)) {
        return capacitance_at_nominal;
    }
    const double reference_potential = GetReferencePotential(thermal, potential_at_nominal);
    const double old_ratio = (potential_at_nominal - reference_potential) / reference_potential;
    const double at_reference =
        capacitance_at_nominal /
        (1.0 + grading_coefficient *
                   (CAPACITANCE_TEMPERATURE_COEFFICIENT * (thermal.NominalKelvin - REFERENCE_TEMPERATURE_KELVIN) -
                    old_ratio));
    const double potential = GetJunctionPotential(thermal, potential_at_nominal);
    const double new_ratio = (potential - reference_potential) / reference_potential;
    return at_reference * (1.0 + grading_coefficient * (CAPACITANCE_TEMPERATURE_COEFFICIENT *
                                                            (thermal.TemperatureKelvin - REFERENCE_TEMPERATURE_KELVIN) -
                                                        new_ratio));
}

} // namespace Nodalys
