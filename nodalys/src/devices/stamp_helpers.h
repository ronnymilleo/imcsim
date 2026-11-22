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

inline double NodeVoltage(const std::vector<double> &x, const int index) {
    return index < 0 ? 0.0 : x[static_cast<std::size_t>(index)];
}

/// Voltage drop $v(a) - v(b)$; ground (`index < 0`) is $0\text{ V}$.
inline double BranchVoltage(const std::vector<double> &x, const int node_a, const int node_b) {
    return NodeVoltage(x, node_a) - NodeVoltage(x, node_b);
}

inline std::complex<double> AcNodeVoltage(const std::vector<std::complex<double>> &x, const int index) {
    return index < 0 ? std::complex<double>{0.0, 0.0} : x[static_cast<std::size_t>(index)];
}

inline std::complex<double>
AcBranchVoltage(const std::vector<std::complex<double>> &x, const int node_a, const int node_b) {
    return AcNodeVoltage(x, node_a) - AcNodeVoltage(x, node_b);
}

inline void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double conductance) {
    sys.AddA(node_a, node_a, conductance);
    sys.AddA(node_b, node_b, conductance);
    sys.AddA(node_a, node_b, -conductance);
    sys.AddA(node_b, node_a, -conductance);
}

/// Stamps a current source pushing `current` from `node_a` into `node_b`.
inline void StampCurrentSource(MnaSystem &sys, const int node_a, const int node_b, const double current) {
    sys.AddRhs(node_a, -current);
    sys.AddRhs(node_b, current);
}

} // namespace Nodalys

#endif // NODALYS_STAMP_HELPERS_H
