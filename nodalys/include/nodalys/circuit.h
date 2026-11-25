/**
 * @file    circuit.h
 * @brief   Compiled immutable circuit representation ready for numerical simulation.
 */

#ifndef NODALYS_CIRCUIT_H
#define NODALYS_CIRCUIT_H

#include "nodalys/types.h"
#include <memory>
#include <string>

namespace Nodalys {

class CircuitImpl;
struct CircuitAccess;

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

    int GetNodeCount() const;
    int GetUnknownCount() const;
    bool HasInitialConditions() const;
    const std::vector<CircuitPort> &GetPorts() const;
    void SetTemperature(double temperature_celsius, double nominal_celsius = 27.0);

private:
    friend struct CircuitAccess;

    std::unique_ptr<CircuitImpl> m_Impl;

    explicit Circuit(std::unique_ptr<CircuitImpl> impl);
};

} // namespace Nodalys

#endif // NODALYS_CIRCUIT_H
