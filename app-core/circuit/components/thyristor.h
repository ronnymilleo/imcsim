/**
 * @file    thyristor.h
 * @brief   A thyristor (SCR) that fires on gate current, latches on, and releases below its holding current.
 */

#ifndef IMCSIM_THYRISTOR_H
#define IMCSIM_THYRISTOR_H

#include "circuit/components/component.h"
#include <memory>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  ThyristorParameters
 * @brief   Datasheet figures of a thyristor (SCR), in SI units.
 * @details The on-state is the datasheet line \f$V_{TM} = V_0 + R_T I_T\f$. Nodalys takes every figure as a parameter;
 *          the ngspice subcircuit follows the gate and slope resistances exactly, while its holding and trigger
 *          currents and its on-state threshold emerge from the two-transistor pair, close to these defaults.
 */
struct ThyristorParameters {
    double OnResistance = 0.01;       ///< Slope resistance \f$R_T\f$ of the on-state line, in Ohms.
    double OnThresholdVoltage = 0.8;  ///< Threshold \f$V_0\f$ of the on-state line, in Volts.
    double OffResistance = 1e9;       ///< Blocking resistance of the off-state, in Ohms.
    double HoldingCurrent = 5e-3;     ///< \f$I_H\f$: anode current below which the latch releases, in Amperes.
    double GateTriggerCurrent = 1e-3; ///< \f$I_{GT}\f$: gate current that fires the device, in Amperes.
    double GateTriggerVoltage = 0.8;  ///< \f$V_{GT}\f$: gate-cathode junction threshold, in Volts.
    double GateResistance = 100.0;    ///< \f$R_G\f$: series gate resistance, in Ohms.
    double BreakoverVoltage = 0.0;    ///< \f$V_{BO}\f$: forward breakover voltage; zero disables it, in Volts.

    bool operator==(const ThyristorParameters &) const = default;
};

/**
 * @class   Thyristor
 * @brief   A three-terminal thyristor (SCR): anode on terminal 1, cathode on terminal 2, gate on terminal 3.
 * @details The part has no value; its parameters are set one by one. Every run starts blocking, and the circuit
 *          fires it. Nodalys simulates it as one latching device; ngspice runs the imcsim_scr two-transistor
 *          subcircuit. The gate drive must be referenced to the cathode, as the gate current returns through it.
 */
class Thyristor : public Component {
public:
    Thyristor();
    ~Thyristor() override = default;

    std::unique_ptr<Component> Clone() const override;

    const ThyristorParameters &GetParameters() const;
    void SetParameters(const ThyristorParameters &parameters);
    static bool IsValidParameters(const ThyristorParameters &parameters);

private:
    ThyristorParameters m_Parameters;
};

// Netlist
const char *GetThyristorSubcircuitName();
std::string FormatThyristorSubcircuit();

} // namespace Core

#endif // IMCSIM_THYRISTOR_H
