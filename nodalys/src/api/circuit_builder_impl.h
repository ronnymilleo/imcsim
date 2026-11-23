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

    explicit CircuitBuilderImpl(int num_circuit_nodes) {
        if (num_circuit_nodes > 0) {
            Description.SetCircuitNodeCount(num_circuit_nodes);
        }
    }

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
