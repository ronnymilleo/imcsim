/**
 * @file    circuit_impl.h
 * @brief   Internal implementation class holding the compiled circuit representation.
 */

#ifndef NODALYS_CIRCUIT_IMPL_H
#define NODALYS_CIRCUIT_IMPL_H

#include "compiler/compiled_circuit.h"
#include <memory>
#include <utility>

namespace nodalys {

class CircuitImpl {
public:
    explicit CircuitImpl(CompiledCircuit compiled) : m_Compiled(std::move(compiled)) {}

    CompiledCircuit &Compiled() { return m_Compiled; }
    const CompiledCircuit &Compiled() const { return m_Compiled; }

    int NodeCount() const { return m_Compiled.CircuitNodeCount; }
    int UnknownCount() const { return m_Compiled.System ? m_Compiled.System->Size() : 0; }
    bool HasInitialConditions() const { return m_Compiled.HasInitialConditions; }

private:
    CompiledCircuit m_Compiled;
};

} // namespace nodalys

#endif // NODALYS_CIRCUIT_IMPL_H
