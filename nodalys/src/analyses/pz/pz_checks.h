/**
 * @file    pz_checks.h
 * @brief   Checks that a pole-zero analysis is worth showing: the input reaches the output and the roots found rebuild
 *          the response of the circuit.
 * @details Both checks solve the small-signal system (G + jwC) x = B at a handful of frequencies around the roots,
 * which is the same answer an AC sweep gives, so they need no root finding of their own.
 */

#ifndef NODALYS_PZ_CHECKS_H
#define NODALYS_PZ_CHECKS_H

#include "linear/dense_matrix.h"
#include "nodalys/results.h"
#include <optional>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  SmallSignalSystem
 * @brief   The linearized circuit G x + C dx/dt = B u with its output y = L x, as ComputePoleZero builds it.
 */
struct SmallSignalSystem {
    DenseMatrix Conductance{0, 0};
    DenseMatrix Reactive{0, 0};
    std::vector<double> Input;
    std::vector<double> Output;
    /// Unknown of the node at the positive terminal of a current input, or -1 for ground and for a voltage input.
    int PortPositive = -1;
    /// Unknown of the node at the negative terminal of a current input, or -1 for ground and for a voltage input.
    int PortNegative = -1;
    bool IsCurrentInput = false;
};

// Checks
[[nodiscard]] std::optional<std::string> FindUndrivenOutput(const SmallSignalSystem &system,
                                                            const PoleZeroResult &roots);
[[nodiscard]] std::optional<std::string> FindRootMismatch(const SmallSignalSystem &system, const PoleZeroResult &roots);

} // namespace Nodalys

#endif // NODALYS_PZ_CHECKS_H
