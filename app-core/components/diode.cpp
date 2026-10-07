/**
 * @file    diode.cpp
 * @brief   Diodes, Zener diodes and LEDs, simulated with a model from a fixed list or with custom parameters.
 */

#include "diode.h"

#include <algorithm>
#include <array>
#include <format>

namespace Core {

namespace {

// Grouped by part, default model first. LEDs are tuned for their forward voltage at 20 mA, Zeners break down at
// their voltage at 5 mA
constexpr auto DiodeModels = std::to_array<DiodeModel>({
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
      .TransitTime = 20e-9},
     ComponentType::Diode},
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
      .TransitTime = 100e-9},
     ComponentType::Diode},
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
      .GradingCoefficient = 0.35},
     ComponentType::Diode},
    {"5V1",
     "Zener diode, 5.1 V",
     "DZ5V1",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 5.1,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12},
     ComponentType::ZenerDiode},
    {"3V3",
     "Zener diode, 3.3 V",
     "DZ3V3",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 3.3,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12},
     ComponentType::ZenerDiode},
    {"12V",
     "Zener diode, 12 V",
     "DZ12V",
     {.SaturationCurrent = 1e-9,
      .EmissionCoefficient = 1.5,
      .SeriesResistance = 1.0,
      .BreakdownVoltage = 12.0,
      .BreakdownCurrent = 5e-3,
      .JunctionCapacitance = 100e-12},
     ComponentType::ZenerDiode},
    {"Red",
     "Red LED, 1.8 V at 20 mA",
     "DLEDRED",
     {.SaturationCurrent = 3.3e-17,
      .EmissionCoefficient = 2.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12},
     ComponentType::LED},
    {"Green",
     "Green LED, 2.1 V at 20 mA",
     "DLEDGREEN",
     {.SaturationCurrent = 9.9e-20,
      .EmissionCoefficient = 2.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12},
     ComponentType::LED},
    {"Blue",
     "Blue LED, 3.0 V at 20 mA",
     "DLEDBLUE",
     {.SaturationCurrent = 5.5e-19,
      .EmissionCoefficient = 3.0,
      .SeriesResistance = 2.0,
      .BreakdownVoltage = 5.0,
      .BreakdownCurrent = 10e-6,
      .JunctionCapacitance = 40e-12},
     ComponentType::LED},
});

} // namespace

/**
 * @brief   Creates a diode part with the default model of its kind.
 * @param[in] type  ComponentType::Diode, ZenerDiode or LED.
 */
Diode::Diode(const ComponentType type)
    : Component(type, 0.0), m_Model(GetDiodeModels(type).data()), m_CustomParameters(m_Model->Parameters) {
}

/**
 * @brief   Returns the ready model the diode uses.
 * @return  An entry of the model list of its kind, or nullptr when the diode uses custom parameters.
 */
const DiodeModel *Diode::GetModel() const {
    return m_Model;
}

/**
 * @brief   Makes the diode use a ready model.
 * @param[in] model  An entry from GetDiodeModels() for the type of this diode; the diode keeps a reference to it.
 */
void Diode::SetModel(const DiodeModel &model) {
    m_Model = &model;
}

/**
 * @brief   Tells whether the diode uses its own parameters instead of a ready model.
 * @return  True for a custom diode.
 */
bool Diode::IsCustom() const {
    return m_Model == nullptr;
}

/**
 * @brief   Switches the diode to custom parameters, starting from those of its current model.
 * @note    Does nothing when the diode is already custom, so its parameters are kept.
 */
void Diode::SetCustom() {
    if (m_Model == nullptr) {
        return;
    }
    m_CustomParameters = m_Model->Parameters;
    m_Model = nullptr;
}

/**
 * @brief   Makes the diode custom with the given parameters.
 * @param[in] parameters  New parameters; check them with IsValidParameters() first.
 */
void Diode::SetCustomParameters(const DiodeParameters &parameters) {
    m_CustomParameters = parameters;
    m_Model = nullptr;
}

/**
 * @brief   Checks whether custom parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when IS, N, BV, IBV and VJ are positive, RS, CJO and TT are not negative, and M is above 0 and
 *          at most 0.9, the largest grading coefficient ngspice accepts.
 */
bool Diode::IsValidParameters(const DiodeParameters &parameters) const {
    return parameters.SaturationCurrent > 0.0 && parameters.EmissionCoefficient > 0.0 &&
           parameters.SeriesResistance >= 0.0 && parameters.BreakdownVoltage > 0.0 &&
           parameters.BreakdownCurrent > 0.0 && parameters.JunctionCapacitance >= 0.0 &&
           parameters.JunctionPotential > 0.0 && parameters.GradingCoefficient > 0.0 &&
           parameters.GradingCoefficient <= 0.9 && parameters.TransitTime >= 0.0;
}

/**
 * @brief   Returns the parameters the diode is simulated with.
 * @return  Those of its ready model, or its custom ones.
 */
const DiodeParameters &Diode::GetParameters() const {
    return m_Model != nullptr ? m_Model->Parameters : m_CustomParameters;
}

/**
 * @brief   Returns the name of the model, as shown in the editor and saved in files.
 * @return  The ready model name, such as "1N4148", or CustomDiodeModelName.
 */
const char *Diode::GetModelName() const {
    return m_Model != nullptr ? m_Model->Name : CustomDiodeModelName;
}

/**
 * @brief   Returns the name of the ".model" line the diode uses in a netlist.
 * @return  The ready model SPICE name, or "DCUSTOM_" followed by the diode name, since custom parameters belong
 *          to one diode.
 */
std::string Diode::GetSpiceModelName() const {
    return m_Model != nullptr ? std::string(m_Model->SpiceName) : std::format("DCUSTOM_{}", GetName());
}

/**
 * @brief   Tells whether a kind of component is a Diode, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for diodes, Zener diodes and LEDs.
 */
bool IsDiode(const ComponentType type) {
    return type == ComponentType::Diode || type == ComponentType::ZenerDiode || type == ComponentType::LED;
}

/**
 * @brief   Returns the ready models a kind of diode part can use.
 * @param[in] type  Kind of component.
 * @return  The models of that part, default first; empty for components that are not diodes.
 */
std::span<const DiodeModel> GetDiodeModels(const ComponentType type) {
    const auto first = std::ranges::find(DiodeModels, type, &DiodeModel::Type);
    const auto last =
        std::find_if(first, DiodeModels.end(), [type](const DiodeModel &model) { return model.Type != type; });
    return {first, last};
}

/**
 * @brief   Finds a ready model of a kind of diode part by name.
 * @param[in] type  Kind of component.
 * @param[in] name  Model name as in DiodeModel::Name, case sensitive.
 * @return  The model, or nullptr when that part has no model with that name.
 */
const DiodeModel *FindDiodeModel(const ComponentType type, const std::string_view name) {
    const std::span<const DiodeModel> models = GetDiodeModels(type);
    const auto model = std::ranges::find(models, name, &DiodeModel::Name);
    return model != models.end() ? &*model : nullptr;
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
                       spice_name, parameters.SaturationCurrent, parameters.EmissionCoefficient,
                       parameters.SeriesResistance, parameters.BreakdownVoltage, parameters.BreakdownCurrent,
                       parameters.JunctionCapacitance, parameters.JunctionPotential, parameters.GradingCoefficient,
                       parameters.TransitTime);
}

} // namespace Core
