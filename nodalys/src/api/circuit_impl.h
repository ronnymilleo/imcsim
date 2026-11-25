/**
 * @file    circuit_impl.h
 * @brief   Internal implementation class holding the compiled circuit representation.
 */

#ifndef NODALYS_CIRCUIT_IMPL_H
#define NODALYS_CIRCUIT_IMPL_H

#include "compiler/compiled_circuit.h"
#include "nodalys/circuit.h"
#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace Nodalys {

class CircuitImpl;

/**
 * @struct  CircuitAccess
 * @brief   Internal bridge into Circuit's PIMPL so public headers never expose CircuitImpl.
 */
struct CircuitAccess {
    [[nodiscard]] static Circuit Create(std::unique_ptr<CircuitImpl> impl);
    static const CompiledCircuit &GetCompiled(const Circuit &circuit);
    [[nodiscard]] static std::expected<CompiledCircuit, std::string> CloneForRun(const Circuit &circuit);
};

/**
 * @class   CircuitImpl
 * @brief   Private PIMPL backing Circuit, owning the compiled MNA blueprint.
 */
class CircuitImpl {
public:
    /**
     * @brief   Constructs the implementation from a compiled circuit blueprint.
     * @param[in] compiled  Compiled circuit blueprint to own.
     */
    explicit CircuitImpl(CompiledCircuit compiled) : m_Compiled(std::move(compiled)) {}

    /**
     * @brief   Returns a mutable reference to the compiled circuit blueprint.
     * @return  Mutable reference to the compiled blueprint.
     */
    CompiledCircuit &Compiled() { return m_Compiled; }

    /**
     * @brief   Returns a read-only reference to the compiled circuit blueprint.
     * @return  Const reference to the compiled blueprint.
     */
    const CompiledCircuit &Compiled() const { return m_Compiled; }

    /**
     * @brief   Returns the number of top-level user-accessible nodes.
     * @return  Top-level node count.
     */
    int GetNodeCount() const { return m_Compiled.CircuitNodeCount; }

    /**
     * @brief   Returns the total number of MNA unknowns in the compiled system.
     * @return  Total MNA unknown count, or `0` if no system was built.
     */
    int GetUnknownCount() const { return m_Compiled.System ? m_Compiled.System->GetSize() : 0; }

    /**
     * @brief   Reports whether any initial conditions were recorded for transient simulation.
     * @return  True if initial conditions exist.
     */
    bool HasInitialConditions() const { return m_Compiled.HasInitialConditions; }

    /**
     * @brief   Returns the configured RF ports for S-parameter analysis.
     * @return  Const reference to the port list.
     */
    const std::vector<CircuitPort> &GetPorts() const { return m_Compiled.Ports; }

private:
    CompiledCircuit m_Compiled;
};

} // namespace Nodalys

#endif // NODALYS_CIRCUIT_IMPL_H
