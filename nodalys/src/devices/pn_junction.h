/**
 * @file    pn_junction.h
 * @brief   The shared physics of a pn junction: the thermal voltage, the critical voltage and the per-iteration
 *          limiting that keeps Newton-Raphson out of the exponential's overflow range.
 * @details Kundert, "The Designer's Guide to SPICE and Spectre", section 4.4, and the PNJLIM routine of SPICE
 *          (Vladimirescu, "The SPICE Book"): the voltage across a junction may change by at most about twice the
 *          thermal voltage per Newton iteration, which damps the oscillations of forward-biased exponentials.
 */

#ifndef NODALYS_PN_JUNCTION_H
#define NODALYS_PN_JUNCTION_H

#include <cmath>

namespace Nodalys {

/// Elementary charge $q$ (C) and Boltzmann constant $k_B$ (J/K) matching modern ngspice (`ngspice/const.h`),
/// evaluated at nominal temperature $T_{\text{nom}} = 300.15\text{ K}$ ($27\,^\circ\text{C}$).
inline constexpr double ELEMENTARY_CHARGE = 1.602176634e-19;
inline constexpr double BOLTZMANN_CONSTANT = 1.380649e-23;
inline constexpr double NOMINAL_TEMPERATURE_KELVIN = 300.15;
inline constexpr double THERMAL_VOLTAGE = 8.617087399994e-5 * NOMINAL_TEMPERATURE_KELVIN;
inline constexpr double FOUR_KT_NOMINAL = 4.0 * BOLTZMANN_CONSTANT * NOMINAL_TEMPERATURE_KELVIN;
inline constexpr double TWO_Q = 2.0 * ELEMENTARY_CHARGE;

/**
 * @brief   The junction voltage above which limiting starts: vt · ln(vt / (IS·√2)).
 * @param[in] saturation_current  IS of the junction.
 * @param[in] thermal_voltage     n·Vt of the junction.
 */
inline double PnCriticalVoltage(const double saturation_current, const double thermal_voltage) {
    return thermal_voltage * std::log(thermal_voltage / (saturation_current * std::sqrt(2.0)));
}

/**
 * @brief   Limits the new junction voltage against the previous iterate, SPICE's PNJLIM.
 * @param[in] voltage_new     Candidate junction voltage of this Newton iteration.
 * @param[in] voltage_old     Junction voltage of the previous iteration.
 * @param[in] critical        From PnCriticalVoltage().
 * @param[in] thermal_voltage n·Vt of the junction.
 * @return  The candidate, folded back towards the previous iterate when it jumps too far.
 */
inline double PnLimit(const double voltage_new, const double voltage_old, const double critical,
                      const double thermal_voltage) {
    if (voltage_new > critical && std::fabs(voltage_new - voltage_old) > 2.0 * thermal_voltage) {
        if (voltage_old > 0.0) {
            const double argument = 1.0 + (voltage_new - voltage_old) / thermal_voltage;
            return argument > 0.0 ? voltage_old + thermal_voltage * std::log(argument) : critical;
        }
        return thermal_voltage * std::log(voltage_new / thermal_voltage);
    }
    return voltage_new;
}

} // namespace Nodalys

#endif // NODALYS_PN_JUNCTION_H
