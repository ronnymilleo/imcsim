/**
 * @file    integration.h
 * @brief   The integration formulas of a transient: how a charge or flux and its history become a companion model.
 * @details Every storage element of a transient (capacitor, inductor, coupled windings, junction and gate charges)
 *          obeys i = dq/dt, with q the charge (or the flux, for an inductor, whose "current" is then the voltage).
 *          Each formula writes the derivative at the new point as i = Coefficient * q + Offset, where Offset holds
 *          what the history contributes, so a device stamps Coefficient times its own dq/dv and Offset only.
 *          Nagel, "SPICE2: A Computer Program to Simulate Semiconductor Circuits", and Gear, "Numerical Initial Value
 *          Problems in Ordinary Differential Equations", define the formulas.
 */

#ifndef NODALYS_INTEGRATION_H
#define NODALYS_INTEGRATION_H

#include "mna/mna_system.h"

namespace Nodalys {

/**
 * @struct  ChargeHistory
 * @brief   What a storage element remembers of the accepted points, as the three formulas need it.
 */
struct ChargeHistory {
    /// Charge or flux at the last accepted point.
    double Charge = 0.0;
    /// Its derivative at that point (the capacitor current, or the inductor voltage); the trapezoidal rule needs it.
    double Current = 0.0;
    /// Charge or flux at the point before the last one; Gear-2 needs it.
    double PreviousCharge = 0.0;
};

/**
 * @struct  Companion
 * @brief   The derivative at the new point, as a linear function of the new charge: i = Coefficient * q + Offset.
 */
struct Companion {
    double Coefficient;
    double Offset;
};

// Companion models
Companion MakeCompanion(const ChargeHistory &history, const TimeContext &time);

// History
ChargeHistory AdvanceHistory(const ChargeHistory &history, double charge, double current);

} // namespace Nodalys

#endif // NODALYS_INTEGRATION_H
