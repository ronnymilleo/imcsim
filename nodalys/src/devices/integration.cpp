/**
 * @file    integration.cpp
 * @brief   The integration formulas of a transient: how a charge or flux and its history become a companion model.
 */

#include "devices/integration.h"

namespace Nodalys {

/**
 * @brief   Builds the companion model of the step described by `time` from the history of the element.
 * @details With h the step and h_p the step before it, and w = h / h_p:
 *          - Backward Euler: i = (q - q_n) / h.
 *          - Trapezoidal: i = 2 (q - q_n) / h - i_n.
 *          - Gear-2 (variable-step BDF-2, the derivative of the parabola through q, q_n and q_(n-1) at the new
 *            point): i = a0 q + a1 q_n + a2 q_(n-1), with a0 = (1 + 2w) / ((1 + w) h), a1 = -(1 + w) / h and
 *            a2 = w^2 / ((1 + w) h). At w = 1 these are the familiar 3/(2h), -2/h and 1/(2h).
 *          Gear-2 falls back to Backward Euler when no previous step is known, which only happens if the caller
 *          did not set `PreviousTimeStep`.
 * @param[in] history  Accepted points of the element.
 * @param[in] time     Step to take: its length, its previous length and the formula.
 * @return  The coefficient and the offset of the derivative at the new point.
 */
Companion MakeCompanion(const ChargeHistory &history, const TimeContext &time) {
    const double step = time.TimeStep;
    const bool is_gear = time.Method == IntegrationMethod::GEAR2;
    if (time.BackwardEuler || (is_gear && time.PreviousTimeStep <= 0.0)) {
        return {.Coefficient = 1.0 / step, .Offset = -history.Charge / step};
    }
    if (is_gear) {
        const double ratio = step / time.PreviousTimeStep;
        const double scale = 1.0 / ((1.0 + ratio) * step);
        return {.Coefficient = (1.0 + 2.0 * ratio) * scale,
                .Offset = -(1.0 + ratio) * (1.0 + ratio) * scale * history.Charge +
                          ratio * ratio * scale * history.PreviousCharge};
    }
    return {.Coefficient = 2.0 / step, .Offset = -2.0 * history.Charge / step - history.Current};
}

/**
 * @brief   Moves the history to a newly accepted point.
 * @param[in] history  History before the point.
 * @param[in] charge   Charge or flux at the point.
 * @param[in] current  Its derivative at the point.
 * @return  The history that follows, whose previous charge is the one the old history ended on.
 */
ChargeHistory AdvanceHistory(const ChargeHistory &history, const double charge, const double current) {
    return {.Charge = charge, .Current = current, .PreviousCharge = history.Charge};
}

} // namespace Nodalys
