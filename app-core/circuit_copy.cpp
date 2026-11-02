/**
 * @file    circuit_copy.cpp
 * @brief   A circuit with a copy of every part, so it can be simulated on another thread while the original is edited.
 */

#include "circuit_copy.h"

#include <map>

namespace Core {

/**
 * @brief   Clones every part of a circuit and wires the clones like the original.
 * @param[in] circuit  Circuit to copy; it is only read here, and the copy does not refer to it afterwards.
 */
CircuitCopy::CircuitCopy(const Circuit &circuit) {
    // A part used by two entries stays one part in the copy
    std::map<const Component *, const Component *> clones;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        auto [clone, is_new] = clones.try_emplace(entry.Part, nullptr);
        if (is_new) {
            m_Parts.push_back(entry.Part->Clone());
            clone->second = m_Parts.back().get();
        }
        m_Circuit.Add(*clone->second, entry.Nodes);
    }
}

/**
 * @brief   Returns the circuit made of the copies.
 * @return  A circuit valid for as long as this object lives and is not moved from.
 */
const Circuit &CircuitCopy::GetCircuit() const {
    return m_Circuit;
}

} // namespace Core
