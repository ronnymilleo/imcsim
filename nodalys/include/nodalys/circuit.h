/**
 * @file    circuit.h
 * @brief   Compiled immutable circuit representation ready for numerical simulation.
 */

#ifndef NODALYS_CIRCUIT_H
#define NODALYS_CIRCUIT_H

#include "nodalys/types.h"
#include <memory>
#include <string>

namespace nodalys {

class CircuitImpl;
class CircuitBuilder;

/**
 * @class   Circuit
 * @brief   An immutable, compiled circuit representation ready for numerical simulation.
 * @details Produced by CircuitBuilder or by SPICE netlist parsing. Contains the stamped MNA system,
 *          device models, terminal current references, noise groups, and dynamic state variables.
 */
class Circuit {
public:
    Circuit();
    ~Circuit();
    Circuit(Circuit &&) noexcept;
    Circuit &operator=(Circuit &&) noexcept;
    Circuit(const Circuit &) = delete;
    Circuit &operator=(const Circuit &) = delete;

    /// Number of top-level user-accessible nodes (results report voltages for 0 .. NodeCount - 1).
    int NodeCount() const;

    /// Total number of MNA unknowns (including internal nodes and branch currents).
    int UnknownCount() const;

    /// Returns true if any reactive element has a non-zero initial condition (uic).
    bool HasInitialConditions() const;

    /// Internal accessor for solver execution.
    explicit Circuit(std::unique_ptr<CircuitImpl> impl);
    CircuitImpl &Impl();
    const CircuitImpl &Impl() const;

private:
    friend class CircuitBuilder;
    std::unique_ptr<CircuitImpl> m_Impl;
};

} // namespace nodalys

#endif // NODALYS_CIRCUIT_H
