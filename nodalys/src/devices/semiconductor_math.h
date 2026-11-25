/**
 * @file    semiconductor_math.h
 * @brief   Helpers shared by the nonlinear devices of Nodalys: node voltages, the stamps of a branch and the depletion
 *          charge and charge companion of a junction.
 * @details
 * ### How Nonlinear Devices Are Solved in MNA
 * At each Newton-Raphson iteration \f$k\f$, every nonlinear branch \f$I(v)\f$ is linearized around the current iterate
 * \f$v^{(k)}\f$ via its first-order Taylor expansion (Norton equivalent companion model):
 *
 * \f[I(v) \approx I(v^{(k)}) + G_{\text{eq}} \cdot (v - v^{(k)}) = G_{\text{eq}} \cdot v + I_{\text{eq}}\f]
 *
 * where:
 * - \f$G_{\text{eq}} = \left.\frac{\partial I}{\partial v}\right|_{v^{(k)}}\f$ is the differential conductance (stamped
 * into \f$A\f$),
 * - \f$I_{\text{eq}} = I(v^{(k)}) - G_{\text{eq}} \cdot v^{(k)}\f$ is the equivalent independent current source
 * (stamped into \f$\text{rhs}\f$).
 *
 * ```text
 *         node_a o----+----[ G_eq ]----+----o node_b
 *                     |                |
 *                     +---(  I_eq  )---+
 *                            --->
 * ```
 *
 * After solving the linear system for \f$x^{(k+1)}\f$, each device's `Limit()` method applies SPICE's `PNJLIM` damping
 * (`pn_junction.h`) so forward-biased junction voltages cannot jump by more than a few thermal voltages \f$V_T\f$ in a
 * single iteration, preventing exponential overflow.
 */

#ifndef NODALYS_SEMICONDUCTOR_MATH_H
#define NODALYS_SEMICONDUCTOR_MATH_H

#include "devices/integration.h"
#include "devices/stamp_helpers.h"
#include "mna/mna_system.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace Nodalys {

/// Forward-bias depletion capacitance cross-over (\f$FC = 0.5\f$) used when the parameters omit it.
inline constexpr double JUNCTION_FORWARD_LIMIT = 0.5;
/// Maximum exponent argument before clamping `exp(x)` so intermediate iterates during gmin stepping cannot overflow.
inline constexpr double MAX_EXPONENT = 80.0;

/**
 * @struct  ChargeCompanion
 * @brief   Linearized companion for a nonlinear charge \f$Q(v)\f$ under the integration method of the step:
 *          \f$i = a\,Q(v) + b \approx G_{\text{cap}} v + I_{\text{eq,cap}}\f$, with \f$a\f$ and \f$b\f$ from
 * `MakeCompanion()`.
 */
struct ChargeCompanion {
    double Conductance;
    double EquivalentCurrent;
};

/**
 * @struct  DepletionCharge
 * @brief   SPICE depletion charge \f$Q(v)\f$ and capacitance \f$C(v) = dQ/dv\f$ of a PN junction.
 * @details Below \f$v = FC \cdot VJ\f$, \f$C(v) = C_{J0} (1 - v/VJ)^{-M}\f$. Above \f$FC \cdot VJ\f$, SPICE switches to
 * a linear extrapolation of \f$C(v)\f$ (quadratic in \f$Q(v)\f$) to avoid the singularity at \f$v = VJ\f$.
 */
struct DepletionCharge {
    double Charge;
    double Capacitance;
};

/**
 * @brief   Evaluates \f$\exp(\min(\text{argument}, \text{MAX\_EXPONENT}))\f$ to prevent floating-point overflow.
 * @param[in] argument  Exponent argument.
 * @return  Clamped exponential value.
 */
inline double Exponential(const double argument) {
    return std::exp(std::min(argument, MAX_EXPONENT));
}

/**
 * @brief   Stamps the Norton companion of a 2-terminal nonlinear branch \f$I(v)\f$ linearized at \f$v_0 = v_a - v_b\f$:
 *          \f$I(v) \approx G_{\text{eq}} \cdot v + I_{\text{eq}}\f$ with \f$I_{\text{eq}} = I(v_0) - G_{\text{eq}}
 * \cdot v_0\f$.
 * @param[in,out] sys            MNA system to stamp into.
 * @param[in]     node_a         First terminal unknown index, or negative for ground.
 * @param[in]     node_b         Second terminal unknown index, or negative for ground.
 * @param[in]     conductance    Differential conductance \f$G_{\text{eq}}\f$ in Siemens.
 * @param[in]     current_at_v0  Branch current \f$I(v_0)\f$ in Amperes.
 * @param[in]     v0             Linearization voltage \f$v_a - v_b\f$ in Volts.
 */
inline void StampNortonBranch(MnaSystem &sys,
                              const int node_a,
                              const int node_b,
                              const double conductance,
                              const double current_at_v0,
                              const double v0) {
    const double ieq = current_at_v0 - conductance * v0;
    StampConductance(sys, node_a, node_b, conductance);
    StampCurrentSource(sys, node_a, node_b, ieq);
}

/**
 * @brief   Stamps a series terminal resistor between `external` and `internal` only when an internal node was
 *          allocated.
 * @param[in,out] sys         MNA system to stamp into.
 * @param[in]     external    External pin unknown index, or negative for ground.
 * @param[in]     internal    Internal node unknown index, or negative when no series resistance is present.
 * @param[in]     resistance  Series resistance in Ohms.
 */
inline void StampSeriesResistor(MnaSystem &sys, const int external, const int internal, const double resistance) {
    if (internal >= 0 && internal != external && resistance > 0.0) {
        StampConductance(sys, external, internal, 1.0 / resistance);
    }
}

/**
 * @brief   Evaluates the SPICE depletion charge \f$Q(v)\f$ and capacitance \f$C(v)\f$ of a PN junction.
 * @param[in] voltage  Forward-positive junction voltage in Volts.
 * @param[in] cj0      Zero-bias junction capacitance in Farads.
 * @param[in] vj       Built-in junction potential in Volts.
 * @param[in] m        Grading coefficient.
 * @param[in] fc       Forward-bias depletion capacitance coefficient.
 * @return  Depletion charge in Coulombs and differential capacitance in Farads.
 */
inline DepletionCharge
EvaluateDepletion(const double voltage, const double cj0, const double vj, const double m, const double fc) {
    if (cj0 <= 0.0) {
        return {0.0, 0.0};
    }
    const double fc_vj = fc * vj;
    if (voltage < fc_vj) {
        const double ratio = std::max(1.0 - voltage / vj, 1e-6);
        const double charge = (cj0 * vj / (1.0 - m)) * (1.0 - std::pow(ratio, 1.0 - m));
        const double capacitance = cj0 * std::pow(ratio, -m);
        return {charge, capacitance};
    }
    // SPICE forward-bias linear capacitance extrapolation for v >= FC * VJ (C^1-continuous at v = FC * VJ):
    //   f1 = (VJ / (1 - M)) * (1 - (1 - FC)^(1 - M))
    //   f2 = (1 - FC)^(1 + M)
    //   f3 = 1 - FC * (1 + M)
    const double f1 = (vj / (1.0 - m)) * (1.0 - std::pow(1.0 - fc, 1.0 - m));
    const double f2 = std::pow(1.0 - fc, 1.0 + m);
    const double f3 = 1.0 - fc * (1.0 + m);
    const double czof2 = cj0 / f2;
    const double dv = voltage - fc_vj;
    const double charge = cj0 * f1 + czof2 * (f3 * dv + (0.5 * m / vj) * (voltage * voltage - fc_vj * fc_vj));
    const double capacitance = czof2 * (f3 + m * voltage / vj);
    return {charge, capacitance};
}

/**
 * @brief   Builds the linearized Norton companion model for a nonlinear junction charge \f$Q(v)\f$ at one transient
 *          Newton iterate.
 * @param[in] charge_derivative  Differential capacitance \f$dQ/dv\f$ at `voltage_iterate` in Farads.
 * @param[in] charge_at_iterate  Junction charge \f$Q(v^{(k)})\f$ in Coulombs.
 * @param[in] voltage_iterate    Current junction voltage iterate \f$v^{(k)}\f$ in Volts.
 * @param[in] history            Accepted charge and current history from previous time points.
 * @param[in] time               Current transient step context and integration method.
 * @return  Equivalent conductance and Norton current source for stamping.
 */
inline ChargeCompanion ChargeModel(const double charge_derivative,
                                   const double charge_at_iterate,
                                   const double voltage_iterate,
                                   const ChargeHistory &history,
                                   const TimeContext &time) {
    const Companion integration = MakeCompanion(history, time);
    const double conductance = integration.Coefficient * charge_derivative;
    const double current_at_iterate = integration.Coefficient * charge_at_iterate + integration.Offset;
    const double equivalent = current_at_iterate - conductance * voltage_iterate;
    return {conductance, equivalent};
}

} // namespace Nodalys

#endif // NODALYS_SEMICONDUCTOR_MATH_H
