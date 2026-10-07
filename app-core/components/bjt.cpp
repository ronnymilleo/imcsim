/**
 * @file    bjt.cpp
 * @brief   NPN and PNP bipolar transistors, simulated with a model from a fixed list or with custom parameters.
 */

#include "bjt.h"

#include <algorithm>
#include <array>
#include <format>

namespace Core {

namespace {

// Grouped by part, default model first. Common vendor cards reduced to the parameters of BJTParameters; the gain
// stays within the datasheet range at the test currents of each part
constexpr auto BJTModels = std::to_array<BJTModel>({
    {"2N3904",
     "General-purpose NPN, 40 V 200 mA",
     "Q2N3904",
     {.SaturationCurrent = 6.734e-15,
      .ForwardBeta = 416.4,
      .ReverseBeta = 0.7371,
      .EarlyVoltage = 74.03,
      .ForwardKneeCurrent = 66.78e-3,
      .LeakageSaturationCurrent = 6.734e-15,
      .LeakageEmissionCoefficient = 1.259,
      .BaseResistance = 10.0,
      .CollectorResistance = 1.0,
      .EmitterCapacitance = 4.493e-12,
      .CollectorCapacitance = 3.638e-12,
      .TransitTime = 301.2e-12},
     ComponentType::NPN},
    {"2N2222A",
     "General-purpose NPN, 40 V 600 mA",
     "Q2N2222A",
     {.SaturationCurrent = 14.34e-15,
      .ForwardBeta = 255.9,
      .ReverseBeta = 6.092,
      .EarlyVoltage = 74.03,
      .ForwardKneeCurrent = 0.2847,
      .LeakageSaturationCurrent = 14.34e-15,
      .LeakageEmissionCoefficient = 1.307,
      .BaseResistance = 10.0,
      .CollectorResistance = 1.0,
      .EmitterCapacitance = 22.01e-12,
      .CollectorCapacitance = 7.306e-12,
      .TransitTime = 411.1e-12},
     ComponentType::NPN},
    {"BC547B",
     "Small-signal NPN, 45 V 100 mA, high gain",
     "QBC547B",
     {.SaturationCurrent = 23.9e-15,
      .ForwardBeta = 294.3,
      .ReverseBeta = 7.946,
      .EarlyVoltage = 63.2,
      .ForwardKneeCurrent = 0.1357,
      .LeakageSaturationCurrent = 3.545e-15,
      .LeakageEmissionCoefficient = 1.541,
      .BaseResistance = 1.0,
      .CollectorResistance = 0.85,
      .EmitterResistance = 0.4683,
      .EmitterCapacitance = 13.58e-12,
      .CollectorCapacitance = 3.728e-12,
      .TransitTime = 439.1e-12},
     ComponentType::NPN},
    {"2N3906",
     "General-purpose PNP, 40 V 200 mA",
     "Q2N3906",
     {.SaturationCurrent = 1.41e-15,
      .ForwardBeta = 180.7,
      .ReverseBeta = 4.977,
      .EarlyVoltage = 18.7,
      .ForwardKneeCurrent = 80e-3,
      .BaseResistance = 10.0,
      .CollectorResistance = 2.5,
      .EmitterCapacitance = 8.063e-12,
      .CollectorCapacitance = 9.728e-12,
      .TransitTime = 179.3e-12},
     ComponentType::PNP},
    {"2N2907A",
     "General-purpose PNP, 60 V 600 mA",
     "Q2N2907A",
     {.SaturationCurrent = 650.6e-18,
      .ForwardBeta = 231.7,
      .ReverseBeta = 3.563,
      .EarlyVoltage = 115.7,
      .ForwardKneeCurrent = 1.079,
      .LeakageSaturationCurrent = 54.81e-15,
      .LeakageEmissionCoefficient = 1.829,
      .BaseResistance = 10.0,
      .CollectorResistance = 0.715,
      .EmitterCapacitance = 19.82e-12,
      .CollectorCapacitance = 14.76e-12,
      .TransitTime = 603.7e-12},
     ComponentType::PNP},
    {"BC557B",
     "Small-signal PNP, 45 V 100 mA, high gain",
     "QBC557B",
     {.SaturationCurrent = 38.3e-15,
      .ForwardBeta = 344.4,
      .ReverseBeta = 14.84,
      .EarlyVoltage = 21.11,
      .ForwardKneeCurrent = 80.39e-3,
      .LeakageSaturationCurrent = 12.2e-15,
      .LeakageEmissionCoefficient = 1.528,
      .BaseResistance = 1.0,
      .CollectorResistance = 0.5713,
      .EmitterResistance = 0.6202,
      .EmitterCapacitance = 12.3e-12,
      .CollectorCapacitance = 10.8e-12,
      .TransitTime = 636e-12},
     ComponentType::PNP},
});

} // namespace

/**
 * @brief   Creates a transistor with the default model of its kind.
 * @param[in] type  ComponentType::NPN or PNP.
 */
BJT::BJT(const ComponentType type)
    : Component(type, 0.0), m_Model(GetBJTModels(type).data()), m_CustomParameters(m_Model->Parameters) {
}

/**
 * @brief   Returns the ready model the transistor uses.
 * @return  An entry of the model list of its kind, or nullptr when it uses custom parameters.
 */
const BJTModel *BJT::GetModel() const {
    return m_Model;
}

/**
 * @brief   Makes the transistor use a ready model.
 * @param[in] model  An entry from GetBJTModels() for the type of this transistor; it keeps a reference to it.
 */
void BJT::SetModel(const BJTModel &model) {
    m_Model = &model;
}

/**
 * @brief   Tells whether the transistor uses its own parameters instead of a ready model.
 * @return  True for a custom transistor.
 */
bool BJT::IsCustom() const {
    return m_Model == nullptr;
}

/**
 * @brief   Switches the transistor to custom parameters, starting from those of its current model.
 * @note    Does nothing when it is already custom, so its parameters are kept.
 */
void BJT::SetCustom() {
    if (m_Model == nullptr) {
        return;
    }
    m_CustomParameters = m_Model->Parameters;
    m_Model = nullptr;
}

/**
 * @brief   Makes the transistor custom with the given parameters.
 * @param[in] parameters  New parameters; check them with IsValidParameters() first.
 */
void BJT::SetCustomParameters(const BJTParameters &parameters) {
    m_CustomParameters = parameters;
    m_Model = nullptr;
}

/**
 * @brief   Checks whether custom parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when IS, BF, BR and NE are positive and everything else is not negative.
 */
bool BJT::IsValidParameters(const BJTParameters &parameters) const {
    return parameters.SaturationCurrent > 0.0 && parameters.ForwardBeta > 0.0 && parameters.ReverseBeta > 0.0 &&
           parameters.EarlyVoltage >= 0.0 && parameters.ForwardKneeCurrent >= 0.0 &&
           parameters.LeakageSaturationCurrent >= 0.0 && parameters.LeakageEmissionCoefficient > 0.0 &&
           parameters.BaseResistance >= 0.0 && parameters.CollectorResistance >= 0.0 &&
           parameters.EmitterResistance >= 0.0 && parameters.EmitterCapacitance >= 0.0 &&
           parameters.CollectorCapacitance >= 0.0 && parameters.TransitTime >= 0.0;
}

/**
 * @brief   Returns the parameters the transistor is simulated with.
 * @return  Those of its ready model, or its custom ones.
 */
const BJTParameters &BJT::GetParameters() const {
    return m_Model != nullptr ? m_Model->Parameters : m_CustomParameters;
}

/**
 * @brief   Returns the name of the model, as shown in the editor and saved in files.
 * @return  The ready model name, such as "2N3904", or CustomBJTModelName.
 */
const char *BJT::GetModelName() const {
    return m_Model != nullptr ? m_Model->Name : CustomBJTModelName;
}

/**
 * @brief   Returns the name of the ".model" line the transistor uses in a netlist.
 * @return  The ready model SPICE name, or "QCUSTOM_" followed by the transistor name, since custom parameters
 *          belong to one transistor.
 */
std::string BJT::GetSpiceModelName() const {
    return m_Model != nullptr ? std::string(m_Model->SpiceName) : std::format("QCUSTOM_{}", GetName());
}

/**
 * @brief   Tells whether a kind of component is a BJT, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for NPN and PNP transistors.
 */
bool IsBJT(const ComponentType type) {
    return type == ComponentType::NPN || type == ComponentType::PNP;
}

/**
 * @brief   Returns the ready models a kind of bipolar transistor can use.
 * @param[in] type  Kind of component.
 * @return  The models of that part, default first; empty for components that are not bipolar transistors.
 */
std::span<const BJTModel> GetBJTModels(const ComponentType type) {
    const auto first = std::ranges::find(BJTModels, type, &BJTModel::Type);
    const auto last =
        std::find_if(first, BJTModels.end(), [type](const BJTModel &model) { return model.Type != type; });
    return {first, last};
}

/**
 * @brief   Finds a ready model of a kind of bipolar transistor by name.
 * @param[in] type  Kind of component.
 * @param[in] name  Model name as in BJTModel::Name, case sensitive.
 * @return  The model, or nullptr when that part has no model with that name.
 */
const BJTModel *FindBJTModel(const ComponentType type, const std::string_view name) {
    const std::span<const BJTModel> models = GetBJTModels(type);
    const auto model = std::ranges::find(models, name, &BJTModel::Name);
    return model != models.end() ? &*model : nullptr;
}

/**
 * @brief   Writes the ".model" line of a bipolar transistor model.
 * @param[in] type        ComponentType::NPN or PNP, which sets the model kind.
 * @param[in] spice_name  Name the transistor lines refer to.
 * @param[in] parameters  Model parameters.
 * @return  The line, newline included, with values in exponent notation and six significant digits.
 */
std::string FormatBJTModel(const ComponentType type, const std::string_view spice_name,
                           const BJTParameters &parameters) {
    return std::format(".model {} {}(IS={:.6g} BF={:.6g} BR={:.6g} VAF={:.6g} IKF={:.6g} ISE={:.6g} NE={:.6g} "
                       "RB={:.6g} RC={:.6g} RE={:.6g} CJE={:.6g} CJC={:.6g} TF={:.6g})\n",
                       spice_name, type == ComponentType::PNP ? "PNP" : "NPN", parameters.SaturationCurrent,
                       parameters.ForwardBeta, parameters.ReverseBeta, parameters.EarlyVoltage,
                       parameters.ForwardKneeCurrent, parameters.LeakageSaturationCurrent,
                       parameters.LeakageEmissionCoefficient, parameters.BaseResistance, parameters.CollectorResistance,
                       parameters.EmitterResistance, parameters.EmitterCapacitance, parameters.CollectorCapacitance,
                       parameters.TransitTime);
}

} // namespace Core
