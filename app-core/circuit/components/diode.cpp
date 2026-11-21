/**
 * @file    diode.cpp
 * @brief   Diodes, Zener diodes and LEDs, simulated with a model from a fixed list or with custom parameters.
 */

#include "circuit/components/diode.h"

#include "circuit/components/model_catalog.h"
#include "circuit/library_model.h"
#include <array>
#include <format>
#include <memory>

namespace Core {

namespace {

/// Grouped by part, default model first. LEDs are tuned for the typical forward voltage at 20 mA of their datasheets
/// below (1.85 V red, 2.2 V green, 3.3 V blue), Zeners break down at
/// their voltage at 5 mA. Ratings are the average forward current and the power dissipation of these datasheets:
/// onsemi 1N914/D Rev. 6 (1N4148: 200 mA, 500 mW); Diodes Inc DS28002 Rev. 11 (1N4001-1N4007: 1 A, and 1.25 W from
/// its 100 K/W to ambient and 150 C junction); onsemi 1N5817/D (1N5819: 1 A, and 1.25 W from its 80 C/W and 125 C
/// junction); Vishay BZX55 document 85604 Rev. 2.0 (500 mW; it gives no forward current limit, so the 200 mA its
/// forward voltage is specified at stands in); Kingbright WP7113SRD/D V.9A (red: 30 mA, 75 mW), WP7113SGD V.11B
/// (green: 25 mA, 62.5 mW) and WP7113QBC/D V.18B (blue: 30 mA, 120 mW).
constexpr auto DIODE_MODELS = std::to_array<DiodeModel>({
    {"1N4148",
     "Small-signal switching diode, 100 V",
     "D1N4148",
     {.SaturationCurrent = 2.52e-9,
      .EmissionCoefficient = 1.752,
      .SeriesResistance = 0.568,
      .BreakdownVoltage = 100.0,
      .BreakdownCurrent = 100e-6,
      .JunctionCapacitance = 4e-12,
      .GradingCoefficient = 0.4,
      .TransitTime = 20e-9,
      .MaxForwardCurrent = 0.2,
      .MaxPower = 0.5},
     ComponentType::DIODE,
     LEDColor::NONE},
    {"1N4007",
     "Rectifier diode, 1000 V 1 A",
     "D1N4007",
     {.SaturationCurrent = 7.02767e-9,
      .EmissionCoefficient = 1.80803,
      .SeriesResistance = 0.0341512,
      .BreakdownVoltage = 1000.0,
      .BreakdownCurrent = 5e-6,
      .JunctionCapacitance = 10e-12,
      .JunctionPotential = 0.7,
      .TransitTime = 100e-9,
      .MaxForwardCurrent = 1.0,
      .MaxPower = 1.25},
     ComponentType::DIODE,
     LEDColor::NONE},
    {"1N5819",
     "Schottky rectifier, 40 V 1 A",
     "D1N5819",
     {.SaturationCurrent = 31.7e-6,
      .EmissionCoefficient = 1.373,
      .SeriesResistance = 0.051,
      .BreakdownVoltage = 40.0,
      .BreakdownCurrent = 1e-3,
      .JunctionCapacitance = 110e-12,
      .JunctionPotential = 0.3,
      .GradingCoefficient = 0.35,
      .MaxForwardCurrent = 1.0,
      .MaxPower = 1.25},
     ComponentType::DIODE,
     LEDColor::NONE},
    {"5V1",
     "Zener diode, 5.1 V",
     "DZ5V1",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 5.1,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12,
      .MaxForwardCurrent = 0.2,
      .MaxPower = 0.5},
     ComponentType::ZENER_DIODE,
     LEDColor::NONE},
    {"3V3",
     "Zener diode, 3.3 V",
     "DZ3V3",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 3.3,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12,
      .MaxForwardCurrent = 0.2,
      .MaxPower = 0.5},
     ComponentType::ZENER_DIODE,
     LEDColor::NONE},
    {"12V",
     "Zener diode, 12 V",
     "DZ12V",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 12.0,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12,
      .MaxForwardCurrent = 0.2,
      .MaxPower = 0.5},
     ComponentType::ZENER_DIODE,
     LEDColor::NONE},
    {"Red",
     "Red LED, 1.85 V at 20 mA",
     "DLEDRED",
     {.SaturationCurrent = 1.255e-17,
      .EmissionCoefficient = 2.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12,
      .MaxForwardCurrent = 30e-3,
      .MaxPower = 75e-3},
     ComponentType::LED,
     LEDColor::RED},
    {"Green",
     "Green LED, 2.2 V at 20 mA",
     "DLEDGREEN",
     {.SaturationCurrent = 1.436e-20,
      .EmissionCoefficient = 2.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12,
      .MaxForwardCurrent = 25e-3,
      .MaxPower = 62.5e-3},
     ComponentType::LED,
     LEDColor::GREEN},
    {"Blue",
     "Blue LED, 3.3 V at 20 mA",
     "DLEDBLUE",
     {.SaturationCurrent = 1.109e-20,
      .EmissionCoefficient = 3.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12,
      .MaxForwardCurrent = 30e-3,
      .MaxPower = 0.12},
     ComponentType::LED,
     LEDColor::BLUE},
});

} // namespace

/**
 * @brief   Creates a diode part with the default model of its kind.
 * @param[in] type  ComponentType::Diode, ZenerDiode or LED.
 */
Diode::Diode(const ComponentType type) : ModeledComponent(type, GetDiodeModels(type).front(), "DCUSTOM_") {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Diode::Clone() const {
    return std::make_unique<Diode>(*this);
}

/**
 * @brief   Checks whether custom parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when IS, N, BV, IBV and VJ are positive, RS, CJO and TT are not negative, and M is above 0 and
 *          at most 0.9, the largest grading coefficient ngspice accepts, and the forward current and power
 *          ratings are positive.
 */
bool Diode::IsValidParameters(const DiodeParameters &parameters) {
    return parameters.SaturationCurrent > 0.0 && parameters.EmissionCoefficient > 0.0
        && parameters.SeriesResistance >= 0.0 && parameters.BreakdownVoltage > 0.0 && parameters.BreakdownCurrent > 0.0
        && parameters.JunctionCapacitance >= 0.0 && parameters.JunctionPotential > 0.0
        && parameters.GradingCoefficient > 0.0 && parameters.GradingCoefficient <= 0.9 && parameters.TransitTime >= 0.0
        && parameters.MaxForwardCurrent > 0.0 && parameters.MaxPower > 0.0;
}

/**
 * @brief   Tells whether a kind of component is a Diode, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for diodes, Zener diodes and LEDs.
 */
bool IsDiode(const ComponentType type) {
    return type == ComponentType::DIODE || type == ComponentType::ZENER_DIODE || type == ComponentType::LED;
}

/**
 * @brief   Returns the ready models a kind of diode part can use.
 * @param[in] type  Kind of component.
 * @return  The models of that part, default first; empty for components that are not diodes.
 */
std::span<const DiodeModel> GetDiodeModels(const ComponentType type) {
    return GetModelsOfType<DiodeModel>(DIODE_MODELS, type);
}

/**
 * @brief   Finds a ready model of a kind of diode part by name.
 * @param[in] type  Kind of component.
 * @param[in] name  Model name as in DiodeModel::Name, case sensitive.
 * @return  The model, or nullptr when that part has no model with that name.
 */
const DiodeModel *FindDiodeModel(const ComponentType type, const std::string_view name) {
    return FindModelByName<DiodeModel>(GetDiodeModels(type), name);
}

/**
 * @brief   Writes the ".model" line of a diode model.
 * @param[in] spice_name  Name the diode lines refer to.
 * @param[in] parameters  Model parameters.
 * @return  The line, newline included. Values use plain exponent notation with six significant digits, since
 *          saturation currents go far below the smallest SPICE suffix.
 */
std::string FormatDiodeModel(const std::string_view spice_name, const DiodeParameters &parameters) {
    return std::format(".model {} D(IS={:.6g} N={:.6g} RS={:.6g} BV={:.6g} IBV={:.6g} CJO={:.6g} VJ={:.6g} M={:.6g} "
                       "TT={:.6g})\n",
                       spice_name,
                       parameters.SaturationCurrent,
                       parameters.EmissionCoefficient,
                       parameters.SeriesResistance,
                       parameters.BreakdownVoltage,
                       parameters.BreakdownCurrent,
                       parameters.JunctionCapacitance,
                       parameters.JunctionPotential,
                       parameters.GradingCoefficient,
                       parameters.TransitTime);
}

} // namespace Core
