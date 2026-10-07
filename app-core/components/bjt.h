/**
 * @file    bjt.h
 * @brief   NPN and PNP bipolar transistors, simulated with a model from a fixed list or with custom parameters.
 */

#ifndef IMCSIM_BJT_H
#define IMCSIM_BJT_H

#include "component.h"
#include <span>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  BJTParameters
 * @brief   Parameters of the SPICE Gummel-Poon transistor model (IS, BF, BR, VAF, IKF, ISE, NE, RB, RC, RE, CJE,
 *          CJC, TF), in SI units.
 * @details Defaults follow ngspice. A VAF or IKF of 0 turns the Early effect or the high-current roll-off off, as
 *          in SPICE. Parameters that only matter away from 27 degrees Celsius are left at their ngspice defaults.
 *          The ratings are not part of the SPICE model: the simulator checks the results against them.
 */
struct BJTParameters {
    double SaturationCurrent = 1e-16;
    double ForwardBeta = 100.0;
    double ReverseBeta = 1.0;
    double EarlyVoltage = 0.0;
    double ForwardKneeCurrent = 0.0;
    // Base-emitter leakage, which lowers the gain at low currents
    double LeakageSaturationCurrent = 0.0;
    double LeakageEmissionCoefficient = 1.5;
    double BaseResistance = 0.0;
    double CollectorResistance = 0.0;
    double EmitterResistance = 0.0;
    double EmitterCapacitance = 0.0;
    double CollectorCapacitance = 0.0;
    double TransitTime = 0.0;
    // Datasheet ratings; the defaults are those of a small-signal part
    double MaxCollectorEmitterVoltage = 40.0;
    double MaxCollectorCurrent = 0.2;
    double MaxPower = 0.5;
};

/**
 * @struct  BJTModel
 * @brief   A ready SPICE transistor model, offered for NPN or PNP parts.
 * @details Name identifies the model in files and in the editor; SpiceName names its ".model" line.
 */
struct BJTModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    BJTParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CustomBJTModelName = "Custom";

/**
 * @class   BJT
 * @brief   A bipolar transistor with collector, base and emitter terminals, in that order.
 * @details The same class serves NPN and PNP parts; the component type tells which models it can use. It has no
 *          numeric value: a ready model or its own custom parameters set its behavior.
 */
class BJT : public Component {
public:
    explicit BJT(ComponentType type);
    ~BJT() override = default;

    // Ready model
    const BJTModel *GetModel() const;
    void SetModel(const BJTModel &model);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const BJTParameters &parameters);
    bool IsValidParameters(const BJTParameters &parameters) const;

    // Simulation
    const BJTParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    // Null when the transistor uses its custom parameters
    const BJTModel *m_Model;
    BJTParameters m_CustomParameters;
};

// Bipolar transistors
bool IsBJT(ComponentType type);
std::span<const BJTModel> GetBJTModels(ComponentType type);
const BJTModel *FindBJTModel(ComponentType type, std::string_view name);
std::string FormatBJTModel(ComponentType type, std::string_view spice_name, const BJTParameters &parameters);

} // namespace Core

#endif // IMCSIM_BJT_H
