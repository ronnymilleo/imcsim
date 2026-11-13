/**
 * @file    thermal.h
 * @brief   The SPICE temperature laws the semiconductor devices of Nodalys share: the thermal voltage, the silicon
 *          band gap, the growth of a saturation current and the shift of a junction potential and capacitance.
 * @details The laws are those of ngspice 47 (`diotemp.c`, `bjttemp.c`, `mos1temp.c`, with the default `tlev` and
 *          `tlevc` of 0), written once here so the diode, the BJT and the MOSFET agree. A model is given at the
 *          nominal temperature $T_{\text{nom}}$ (`TNOM`, 27 °C unless the deck says otherwise) and simulated at $T$:
 *
 *          $$V_t = \frac{k T}{q}, \qquad E_g(T) = 1.16 - \frac{7.02\cdot10^{-4}\, T^2}{T + 1108}$$
 *
 *          A saturation current grows as $I_S(T) = I_S \exp\left(\left(\frac{T}{T_{nom}} - 1\right)
 *          \frac{E_G}{V_t(T)} + X_{TI} \ln\frac{T}{T_{nom}}\right)$ (a diode divides the exponent by its emission
 *          coefficient, a BJT leakage current too). A junction potential follows the band gap and the thermal voltage:
 *          $V_J(T) = \frac{T}{T_{ref}} V_{J0} + P(T)$ with $V_{J0} = (V_J - P(T_{nom})) \frac{T_{ref}}{T_{nom}}$ and
 *          $P(T) = E_g(T) - 1.1150877 \frac{T}{T_{ref}} - 3 V_t(T) \ln\frac{T}{T_{ref}}$, where $T_{ref} = 300.15$ K;
 *          the zero-bias capacitance follows it through $C_{J}(T) = C_J (1 + M (4\cdot10^{-4}(T - T_{ref}) -
 *          \gamma_T))$ with $\gamma_T = (V_J(T) - V_{J0}) / V_{J0}$, divided by the same factor at $T_{nom}$.
 */

#ifndef NODALYS_THERMAL_H
#define NODALYS_THERMAL_H

#include "devices/pn_junction.h"

namespace Nodalys {

/**
 * @struct  ThermalConditions
 * @brief   The temperature of a run and the nominal temperature the models are given at, in kelvin.
 */
struct ThermalConditions {
    double TemperatureKelvin = NOMINAL_TEMPERATURE_KELVIN;
    double NominalKelvin = NOMINAL_TEMPERATURE_KELVIN;

    bool operator==(const ThermalConditions &) const = default;
};

/// The temperature the junction laws of SPICE refer to, $27\,^\circ\text{C}$ (`REFTEMP`), whatever `TNOM` is.
inline constexpr double REFERENCE_TEMPERATURE_KELVIN = 300.15;

/// Offset between degrees Celsius and kelvin, as ngspice (`CONSTCtoK`).
inline constexpr double CELSIUS_TO_KELVIN = 273.15;

/// Band gap of silicon at 0 K and the two constants of its temperature law, in volts, volts per kelvin and kelvin.
inline constexpr double BAND_GAP_AT_ZERO_KELVIN = 1.16;
inline constexpr double BAND_GAP_SLOPE = 7.02e-4;
inline constexpr double BAND_GAP_OFFSET_KELVIN = 1108.0;

// Conditions
ThermalConditions MakeThermalConditions(double temperature_celsius, double nominal_celsius);
bool IsNominal(const ThermalConditions &thermal);
double GetThermalVoltage(const ThermalConditions &thermal);
double GetFourKt(const ThermalConditions &thermal);

// Laws
double GetBandGap(double temperature_kelvin);
double GetSaturationCurrentLogRatio(const ThermalConditions &thermal, double energy_gap,
                                    double saturation_current_exponent);
double GetJunctionPotential(const ThermalConditions &thermal, double potential_at_nominal);
double GetJunctionCapacitance(const ThermalConditions &thermal, double capacitance_at_nominal,
                              double potential_at_nominal, double grading_coefficient);

} // namespace Nodalys

#endif // NODALYS_THERMAL_H
