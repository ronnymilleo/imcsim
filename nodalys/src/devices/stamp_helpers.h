/**
 * @file    stamp_helpers.h
 * @brief   Inline MNA stamp and node-voltage helpers shared by Nodalys device implementations.
 */

#ifndef NODALYS_STAMP_HELPERS_H
#define NODALYS_STAMP_HELPERS_H

#include "mna/mna_system.h"
#include <complex>
#include <cstddef>
#include <vector>

namespace Nodalys {

/**
 * @brief   Reads a node voltage from a real MNA solution vector, returning zero for ground.
 * @param[in] x      Real MNA solution vector.
 * @param[in] index  Zero-based unknown index, or negative for ground.
 * @return  Node voltage in Volts, or `0.0` for ground.
 */
inline double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

/**
 * @brief   Computes the real voltage drop \f$v(a) - v(b)\f$; ground (`index < 0`) is \f$0\text{ V}\f$.
 * @param[in] x       Real MNA solution vector.
 * @param[in] node_a  Positive node unknown index, or negative for ground.
 * @param[in] node_b  Negative node unknown index, or negative for ground.
 * @return  Voltage difference \f$v(a) - v(b)\f$ in Volts.
 */
inline double BranchVoltage(const std::vector<double> &x, const int node_a, const int node_b) {
    return NodeVoltage(x, node_a) - NodeVoltage(x, node_b);
}

/**
 * @brief   Reads a node voltage phasor from a complex AC solution vector, returning zero for ground.
 * @param[in] x      Complex AC solution vector.
 * @param[in] index  Zero-based unknown index, or negative for ground.
 * @return  Complex node voltage phasor in Volts, or `0` for ground.
 */
inline std::complex<double> AcNodeVoltage(const std::vector<std::complex<double>> &x, const int index) {
    return index < 0 ? std::complex<double>{0.0, 0.0} : x[static_cast<std::size_t>(index)];
}

/**
 * @brief   Computes the complex small-signal voltage drop \f$V(a) - V(b)\f$.
 * @param[in] x       Complex AC solution vector.
 * @param[in] node_a  Positive node unknown index, or negative for ground.
 * @param[in] node_b  Negative node unknown index, or negative for ground.
 * @return  Complex voltage phasor difference in Volts.
 */
inline std::complex<double>
AcBranchVoltage(const std::vector<std::complex<double>> &x, const int node_a, const int node_b) {
    return AcNodeVoltage(x, node_a) - AcNodeVoltage(x, node_b);
}

/**
 * @brief   Stamps a two-terminal conductance between `node_a` and `node_b` into the MNA matrix.
 * @param[in,out] sys          MNA system to stamp into.
 * @param[in]     node_a       First terminal unknown index, or negative for ground.
 * @param[in]     node_b       Second terminal unknown index, or negative for ground.
 * @param[in]     conductance  Conductance in Siemens.
 */
inline void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double conductance) {
    sys.AddA(node_a, node_a, conductance);
    sys.AddA(node_b, node_b, conductance);
    sys.AddA(node_a, node_b, -conductance);
    sys.AddA(node_b, node_a, -conductance);
}

/**
 * @brief   Stamps an independent current source pushing `current` from `node_a` into `node_b`.
 * @param[in,out] sys      MNA system to stamp into.
 * @param[in]     node_a   Source node unknown index (current leaves here), or negative for ground.
 * @param[in]     node_b   Sink node unknown index (current enters here), or negative for ground.
 * @param[in]     current  Current magnitude in Amperes.
 */
inline void StampCurrentSource(MnaSystem &sys, const int node_a, const int node_b, const double current) {
    sys.AddRhs(node_a, -current);
    sys.AddRhs(node_b, current);
}

} // namespace Nodalys

#endif // NODALYS_STAMP_HELPERS_H
