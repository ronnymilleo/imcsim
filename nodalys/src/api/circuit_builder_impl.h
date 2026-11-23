/**
 * @file    circuit_builder_impl.h
 * @brief   Internal PIMPL definition shared across CircuitBuilder translation units.
 */

#ifndef NODALYS_API_CIRCUIT_BUILDER_IMPL_H
#define NODALYS_API_CIRCUIT_BUILDER_IMPL_H

#include "compiler/circuit_description.h"
#include "compiler/source_specs.h"
#include "nodalys/types.h"
#include <string>
#include <utility>

namespace Nodalys {

/**
 * @class   CircuitBuilderImpl
 * @brief   Holds the passive CircuitDescription collected by CircuitBuilder prior to MNA compilation.
 */
class CircuitBuilderImpl {
public:
    CircuitDescription Description;
    bool HasBuilt = false;

    /**
     * @brief   Initializes the builder storage with an optional initial circuit node count.
     * @param[in] num_circuit_nodes  Minimum number of top-level nodes (including ground).
     */
    explicit CircuitBuilderImpl(int num_circuit_nodes) {
        if (num_circuit_nodes > 0) {
            Description.SetCircuitNodeCount(num_circuit_nodes);
        }
    }

    /**
     * @brief   Appends an independent voltage or current source specification and tracks its terminals.
     * @param[in] name        Element designator.
     * @param[in] pos         Positive terminal node.
     * @param[in] neg         Negative terminal node.
     * @param[in] is_voltage  True for a voltage source, false for a current source.
     * @param[in] spec        Waveform and AC excitation specification.
     */
    void AddSource(std::string name, NodeId pos, NodeId neg, bool is_voltage, SourceSpec spec) {
        Description.TrackNodes({pos, neg});
        Description.Elements.push_back(IndependentSourceSpec{
            .Name = std::move(name),
            .Pos = pos,
            .Neg = neg,
            .IsVoltage = is_voltage,
            .Spec = std::move(spec),
        });
    }
};

} // namespace Nodalys

#endif // NODALYS_API_CIRCUIT_BUILDER_IMPL_H
