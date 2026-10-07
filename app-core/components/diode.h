/**
 * @file    diode.h
 * @brief   Diodes, Zener diodes and LEDs, simulated with a model from a fixed list or with custom parameters.
 */

#ifndef IMCSIM_DIODE_H
#define IMCSIM_DIODE_H

#include "component.h"
#include <span>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  DiodeParameters
 * @brief   Parameters of the SPICE diode model (IS, N, RS, BV, IBV, CJO, VJ, M, TT), in SI units.
 * @details Defaults follow ngspice, except BV: ngspice has no breakdown unless BV is given, and every part here
 *          has one. Parameters that only matter away from 27 degrees Celsius are left at their ngspice defaults.
 */
struct DiodeParameters {
    double SaturationCurrent = 1e-14;
    double EmissionCoefficient = 1.0;
    double SeriesResistance = 0.0;
    // For a Zener, the voltage it regulates at
    double BreakdownVoltage = 100.0;
    double BreakdownCurrent = 1e-3;
    double JunctionCapacitance = 0.0;
    double JunctionPotential = 1.0;
    double GradingCoefficient = 0.5;
    double TransitTime = 0.0;
};

/**
 * @struct  DiodeModel
 * @brief   A ready SPICE diode model, offered for one kind of diode part.
 * @details Name identifies the model in files and in the editor; SpiceName names its ".model" line.
 */
struct DiodeModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    DiodeParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CustomDiodeModelName = "Custom";

/**
 * @class   Diode
 * @brief   A diode between an anode (first terminal) and a cathode (second terminal).
 * @details The same class serves diodes, Zener diodes and LEDs; the component type tells which models it can use.
 *          It has no numeric value: a ready model or its own custom parameters set its behavior.
 */
class Diode : public Component {
public:
    explicit Diode(ComponentType type);
    ~Diode() override = default;

    // Ready model
    const DiodeModel *GetModel() const;
    void SetModel(const DiodeModel &model);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const DiodeParameters &parameters);
    bool IsValidParameters(const DiodeParameters &parameters) const;

    // Simulation
    const DiodeParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    // Null when the diode uses its custom parameters
    const DiodeModel *m_Model;
    DiodeParameters m_CustomParameters;
};

// Diodes
bool IsDiode(ComponentType type);
std::span<const DiodeModel> GetDiodeModels(ComponentType type);
const DiodeModel *FindDiodeModel(ComponentType type, std::string_view name);
std::string FormatDiodeModel(std::string_view spice_name, const DiodeParameters &parameters);

} // namespace Core

#endif // IMCSIM_DIODE_H
