/**
 * @file    mosfet.cpp
 * @brief   N- and P-channel MOSFETs, simulated with a model from a fixed list or with custom parameters.
 */

#include "circuit/components/mosfet.h"

#include "circuit/components/model_catalog.h"
#include "circuit/library_model.h"
#include <array>
#include <format>
#include <memory>

namespace Core {

namespace {

/// Grouped by part, default model first. Level 1 approximations: VTO is the typical threshold, KP sets the
/// datasheet RDS(on) at |VGS| = 10 V, and the overlaps, in F per meter of width, give the typical input and reverse
/// transfer capacitances with the default 100 um width (20 pF is 2e-7 F/m). Ratings are the absolute maximum VDS, VGS
/// and continuous ID, and the dissipation in free air, or with an ideal heatsink for the power parts, from these
/// datasheets: onsemi 2N7000TA/D Rev. 1 (60 V, 30 V, 200 mA, 400 mW), onsemi BS170/D Rev. 7 (60 V, 20 V, 500 mA,
/// 350 mW), Fairchild IRF540N Rev. C (100 V, 20 V, 33 A, 120 W), Philips BS250 of April 1995 (45 V, 250 mA,
/// 830 mW). The IRF9540N ratings are those of International Rectifier's PD-91437B, not checked against a copy.
constexpr auto MOSFET_MODELS = std::to_array<MOSFETModel>({
    {"2N7000",
     "Small-signal N-channel, 60 V 200 mA, 1.8 Ohm",
     "M2N7000",
     {.ThresholdVoltage = 2.1,
      .Transconductance = 0.0703,
      .ChannelModulation = 0.01,
      .GateSourceOverlap = 2e-07,
      .GateDrainOverlap = 5e-08,
      .MaxDrainSourceVoltage = 60.0,
      .MaxGateSourceVoltage = 30.0,
      .MaxDrainCurrent = 0.2,
      .MaxPower = 0.4},
     ComponentType::NMOS},
    {"BS170",
     "Small-signal N-channel, 60 V 500 mA, 1.2 Ohm",
     "MBS170",
     {.ThresholdVoltage = 2.1,
      .Transconductance = 0.105,
      .ChannelModulation = 0.01,
      .GateSourceOverlap = 2.4e-07,
      .GateDrainOverlap = 5e-08,
      .MaxDrainSourceVoltage = 60.0,
      .MaxGateSourceVoltage = 20.0,
      .MaxDrainCurrent = 0.5,
      .MaxPower = 0.35},
     ComponentType::NMOS},
    {"IRF540N",
     "Power N-channel, 100 V 33 A, 44 mOhm, simplified body diode",
     "MIRF540N",
     {.ThresholdVoltage = 3.0,
      .Transconductance = 3.25,
      .ChannelModulation = 0.002,
      .GateSourceOverlap = 1.96e-05,
      .GateDrainOverlap = 4e-07,
      .MaxDrainSourceVoltage = 100.0,
      .MaxGateSourceVoltage = 20.0,
      .MaxDrainCurrent = 33,
      .MaxPower = 120.0},
     ComponentType::NMOS},
    {"BS250",
     "Small-signal P-channel, 45 V 250 mA, 14 Ohm",
     "MBS250",
     {.ThresholdVoltage = -2.0,
      .Transconductance = 0.0089,
      .ChannelModulation = 0.01,
      .GateSourceOverlap = 6e-07,
      .GateDrainOverlap = 1e-07,
      .MaxDrainSourceVoltage = 45.0,
      .MaxGateSourceVoltage = 20.0,
      .MaxDrainCurrent = 0.25,
      .MaxPower = 0.83},
     ComponentType::PMOS},
    {"IRF9540N",
     "Power P-channel, 100 V 23 A, 117 mOhm, simplified body diode",
     "MIRF9540N",
     {.ThresholdVoltage = -3.0,
      .Transconductance = 1.22,
      .ChannelModulation = 0.002,
      .GateSourceOverlap = 1.3e-05,
      .GateDrainOverlap = 3e-07,
      .MaxDrainSourceVoltage = 100.0,
      .MaxGateSourceVoltage = 20.0,
      .MaxDrainCurrent = 23,
      .MaxPower = 140.0},
     ComponentType::PMOS},
});

} // namespace

/**
 * @brief   Creates a MOSFET with the default model of its kind.
 * @param[in] type  ComponentType::NMOS or PMOS.
 */
MOSFET::MOSFET(const ComponentType type) : ModeledComponent(type, GetMOSFETModels(type).front(), "MCUSTOM_") {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> MOSFET::Clone() const {
    return std::make_unique<MOSFET>(*this);
}

/**
 * @brief   Checks whether custom parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when KP, W, L and the ratings are positive and LAMBDA, RD, RS, CGSO and CGDO are not negative. VTO
 *          may have either sign, so depletion parts can be modeled too.
 */
bool MOSFET::IsValidParameters(const MOSFETParameters &parameters) {
    return parameters.Transconductance > 0.0 && parameters.ChannelModulation >= 0.0 && parameters.DrainResistance >= 0.0
        && parameters.SourceResistance >= 0.0 && parameters.GateSourceOverlap >= 0.0
        && parameters.GateDrainOverlap >= 0.0 && parameters.Width > 0.0 && parameters.Length > 0.0
        && parameters.MaxDrainSourceVoltage > 0.0 && parameters.MaxGateSourceVoltage > 0.0
        && parameters.MaxDrainCurrent > 0.0 && parameters.MaxPower > 0.0;
}

/**
 * @brief   Tells whether a kind of component is a MOSFET, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for NMOS and PMOS transistors.
 */
bool IsMOSFET(const ComponentType type) {
    return type == ComponentType::NMOS || type == ComponentType::PMOS;
}

/**
 * @brief   Returns the ready models a kind of MOSFET can use.
 * @param[in] type  Kind of component.
 * @return  The models of that part, default first; empty for components that are not MOSFETs.
 */
std::span<const MOSFETModel> GetMOSFETModels(const ComponentType type) {
    return GetModelsOfType<MOSFETModel>(MOSFET_MODELS, type);
}

/**
 * @brief   Finds a ready model of a kind of MOSFET by name.
 * @param[in] type  Kind of component.
 * @param[in] name  Model name as in MOSFETModel::Name, case sensitive.
 * @return  The model, or nullptr when that part has no model with that name.
 */
const MOSFETModel *FindMOSFETModel(const ComponentType type, const std::string_view name) {
    return FindModelByName<MOSFETModel>(GetMOSFETModels(type), name);
}

/**
 * @brief   Writes the ".model" line of a MOSFET model.
 * @param[in] type        ComponentType::NMOS or PMOS, which sets the model kind.
 * @param[in] spice_name  Name the MOSFET lines refer to.
 * @param[in] parameters  Model parameters; W and L go on the MOSFET line instead, since SPICE sets them per part.
 * @return  The line, newline included, with values in exponent notation and six significant digits.
 */
std::string
FormatMOSFETModel(const ComponentType type, const std::string_view spice_name, const MOSFETParameters &parameters) {
    return std::format(".model {} {}(LEVEL=1 VTO={:.6g} KP={:.6g} LAMBDA={:.6g} RD={:.6g} RS={:.6g} CGSO={:.6g} "
                       "CGDO={:.6g})\n",
                       spice_name,
                       type == ComponentType::PMOS ? "PMOS" : "NMOS",
                       parameters.ThresholdVoltage,
                       parameters.Transconductance,
                       parameters.ChannelModulation,
                       parameters.DrainResistance,
                       parameters.SourceResistance,
                       parameters.GateSourceOverlap,
                       parameters.GateDrainOverlap);
}

} // namespace Core
