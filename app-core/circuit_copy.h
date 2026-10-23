/**
 * @file    circuit_copy.h
 * @brief   A circuit with a copy of every part, so it can be simulated on another thread while the original is edited.
 */

#ifndef IMCSIM_CIRCUIT_COPY_H
#define IMCSIM_CIRCUIT_COPY_H

#include "circuit.h"
#include <memory>
#include <vector>

namespace Core {

/**
 * @class   CircuitCopy
 * @brief   Owns clones of the parts of a circuit and the circuit that wires them like the original.
 * @details A Circuit only points at its parts, which the schematic owns and the user can change at any moment. A
 *          simulation on the worker thread takes a CircuitCopy instead, so there is nothing shared to race on. It can
 *          be moved, since the circuit points at the parts on the heap, but not copied.
 */
class CircuitCopy {
public:
    explicit CircuitCopy(const Circuit &circuit);

    CircuitCopy(const CircuitCopy &) = delete;
    CircuitCopy &operator=(const CircuitCopy &) = delete;
    CircuitCopy(CircuitCopy &&) noexcept = default;
    CircuitCopy &operator=(CircuitCopy &&) noexcept = default;
    ~CircuitCopy() = default;

    const Circuit &GetCircuit() const;

private:
    std::vector<std::unique_ptr<Component>> m_Parts;
    Circuit m_Circuit;
};

} // namespace Core

#endif // IMCSIM_CIRCUIT_COPY_H
