/**
 * @file    circuit_copy.cpp
 * @brief   A circuit with a copy of every part, so it can be simulated on another thread while the original is edited.
 */

#include "simulation/circuit_copy.h"

#include "model/subcircuit.h"
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
    for (const auto &definition : circuit.GetSubcircuits()) {
        auto body = std::make_unique<CircuitCopy>(definition->Body);
        auto copy = std::make_shared<SubcircuitDefinition>(*definition);
        copy->Body = body->GetCircuit();
        m_SubcircuitCopies.push_back(std::move(body));
        m_Circuit.AddSubcircuit(std::move(copy));
    }
    for (const LibraryModel &model : circuit.GetLibraryModels()) {
        m_Circuit.AddLibraryModel(model);
    }
    for (const CircuitPort &port : circuit.GetPorts()) {
        m_Circuit.AddPort(port);
    }
    for (const auto &[net, node] : circuit.GetNets()) {
        m_Circuit.NameNet(net, node);
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
