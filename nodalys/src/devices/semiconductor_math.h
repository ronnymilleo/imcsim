/**
 * @file    semiconductor_math.h
 * @brief   Helpers shared by the nonlinear devices of Nodalys: node voltages, the stamps of a branch and the depletion
 *          charge and charge companion of a junction.
 * @details
 * ### How Nonlinear Devices Are Solved in MNA
 * At each Newton-Raphson iteration $k$, every nonlinear branch $I(v)$ is linearized around the current iterate
 * $v^{(k)}$ via its first-order Taylor expansion (Norton equivalent companion model):
 *
 * $$I(v) \approx I(v^{(k)}) + G_{\text{eq}} \cdot (v - v^{(k)}) = G_{\text{eq}} \cdot v + I_{\text{eq}}$$
 *
 * where:
 * - $G_{\text{eq}} = \left.\frac{\partial I}{\partial v}\right|_{v^{(k)}}$ is the differential conductance (stamped
 * into $A$),
 * - $I_{\text{eq}} = I(v^{(k)}) - G_{\text{eq}} \cdot v^{(k)}$ is the equivalent independent current source (stamped
 * into $\text{rhs}$).
 *
 * ```text
 *         node_a o----+----[ G_eq ]----+----o node_b
 *                     |                |
 *                     +---(  I_eq  )---+
 *                            --->
 * ```
 *
 * After solving the linear system for $x^{(k+1)}$, each device's `Limit()` method applies SPICE's `PNJLIM` damping
 * (`pn_junction.h`) so forward-biased junction voltages cannot jump by more than a few thermal voltages $V_T$ in a
 * single iteration, preventing exponential overflow.
 */

#ifndef NODALYS_SEMICONDUCTOR_MATH_H
#define NODALYS_SEMICONDUCTOR_MATH_H

#include "mna/mna_system.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace Nodalys {

/// Maximum exponent argument before clamping `exp(x)` so intermediate iterates during gmin stepping cannot overflow.
inline constexpr double MAX_EXPONENT = 80.0;
/// Forward-bias depletion capacitance cross-over ($FC = 0.5$) used when the parameters omit it.
inline constexpr double JUNCTION_FORWARD_LIMIT = 0.5;

inline double Exponential(const double argument) {
    return std::exp(std::min(argument, MAX_EXPONENT));
}

inline double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

inline std::complex<double> AcNodeVoltage(const std::vector<std::complex<double>> &x, const int index) {
    return index < 0 ? std::complex<double>{0.0, 0.0} : x[static_cast<std::size_t>(index)];
}

inline void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double conductance) {
    sys.AddA(node_a, node_a, conductance);
    sys.AddA(node_b, node_b, conductance);
    sys.AddA(node_a, node_b, -conductance);
    sys.AddA(node_b, node_a, -conductance);
}

inline void StampCurrentSource(MnaSystem &sys, const int node_a, const int node_b, const double current) {
    sys.AddRhs(node_a, -current);
    sys.AddRhs(node_b, current);
}

/**
 * @brief   Stamps the Norton companion of a 2-terminal nonlinear branch $I(v)$ linearized at $v_0 = v_a - v_b$:
 *          $I(v) \approx G_{\text{eq}} \cdot v + I_{\text{eq}}$ with $I_{\text{eq}} = I(v_0) - G_{\text{eq}} \cdot
 * v_0$.
 */
inline void StampNortonBranch(MnaSystem &sys, const int node_a, const int node_b, const double conductance,
                              const double current_at_v0, const double v0) {
    const double ieq = current_at_v0 - conductance * v0;
    StampConductance(sys, node_a, node_b, conductance);
    StampCurrentSource(sys, node_a, node_b, ieq);
}

/// Stamps a series terminal resistor between `external` and `internal` only when an internal node was allocated.
inline void StampSeriesResistor(MnaSystem &sys, const int external, const int internal, const double resistance) {
    if (internal >= 0 && internal != external && resistance > 0.0) {
        StampConductance(sys, external, internal, 1.0 / resistance);
    }
}

/**
 * @struct  DepletionCharge
 * @brief   SPICE depletion charge $Q(v)$ and capacitance $C(v) = dQ/dv$ of a PN junction.
 * @details Below $v = FC \cdot VJ$, $C(v) = C_{J0} (1 - v/VJ)^{-M}$. Above $FC \cdot VJ$, SPICE switches to a linear
 *          extrapolation of $C(v)$ (quadratic in $Q(v)$) to avoid the singularity at $v = VJ$.
 */
struct DepletionCharge {
    double Charge;
    double Capacitance;
};

inline DepletionCharge EvaluateDepletion(const double voltage, const double cj0, const double vj, const double m,
                                         const double fc) {
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
 * @struct  ChargeCompanion
 * @brief   Linearized Trapezoidal (or Backward Euler) companion for a nonlinear charge $Q(v)$:
 *          $i(t+h) = \frac{\text{factor}}{h} (Q(v) - Q(t)) - i_{\text{past}} \approx G_{\text{cap}} v +
 * I_{\text{eq,cap}}$.
 */
struct ChargeCompanion {
    double Conductance;
    double EquivalentCurrent;
};

inline ChargeCompanion ChargeModel(const double charge_derivative, const double charge_at_iterate,
                                   const double voltage_iterate, const double charge_previous,
                                   const double current_previous, const double step, const bool backward_euler) {
    const double factor = backward_euler ? 1.0 : 2.0;
    const double conductance = factor * charge_derivative / step;
    const double current_at_iterate =
        factor * (charge_at_iterate - charge_previous) / step - (backward_euler ? 0.0 : current_previous);
    const double equivalent = current_at_iterate - conductance * voltage_iterate;
    return {conductance, equivalent};
}

} // namespace Nodalys

#endif // NODALYS_SEMICONDUCTOR_MATH_H
