/**
 * @file    mosfet.h
 * @brief   N- and P-channel MOSFETs, simulated with a model from a fixed list or with custom parameters.
 */

#ifndef IMCSIM_MOSFET_H
#define IMCSIM_MOSFET_H

#include "component.h"
#include <span>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  MOSFETParameters
 * @brief   Parameters of the SPICE level 1 MOSFET model (VTO, KP, LAMBDA, RD, RS, CGSO, CGDO) and the channel
 *          size (W, L), in SI units.
 * @details Defaults follow ngspice. The drain current in saturation is KP/2 * W/L * (VGS - VTO)^2, so only the
 *          ratio W/L matters for it; W also scales the gate overlap capacitances, given per meter of width. Level 1
 *          has no body diode.
 */
struct MOSFETParameters {
    double ThresholdVoltage = 0.0;
    double Transconductance = 2e-5;
    double ChannelModulation = 0.0;
    double DrainResistance = 0.0;
    double SourceResistance = 0.0;
    double GateSourceOverlap = 0.0;
    double GateDrainOverlap = 0.0;
    double Width = 100e-6;
    double Length = 100e-6;
};

/**
 * @struct  MOSFETModel
 * @brief   A ready SPICE MOSFET model, offered for NMOS or PMOS parts.
 * @details Name identifies the model in files and in the editor; SpiceName names its ".model" line.
 */
struct MOSFETModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    MOSFETParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CustomMOSFETModelName = "Custom";

/**
 * @class   MOSFET
 * @brief   An enhancement MOSFET with drain, gate and source terminals, in that order, and its body tied to the
 *          source.
 * @details The same class serves NMOS and PMOS parts; the component type tells which models it can use. It has no
 *          numeric value: a ready model or its own custom parameters set its behavior.
 */
class MOSFET : public Component {
public:
    explicit MOSFET(ComponentType type);
    ~MOSFET() override = default;

    // Ready model
    const MOSFETModel *GetModel() const;
    void SetModel(const MOSFETModel &model);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const MOSFETParameters &parameters);
    bool IsValidParameters(const MOSFETParameters &parameters) const;

    // Simulation
    const MOSFETParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    // Null when the transistor uses its custom parameters
    const MOSFETModel *m_Model;
    MOSFETParameters m_CustomParameters;
};

// MOSFETs
bool IsMOSFET(ComponentType type);
std::span<const MOSFETModel> GetMOSFETModels(ComponentType type);
const MOSFETModel *FindMOSFETModel(ComponentType type, std::string_view name);
std::string FormatMOSFETModel(ComponentType type, std::string_view spice_name, const MOSFETParameters &parameters);

} // namespace Core

#endif // IMCSIM_MOSFET_H
