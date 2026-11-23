/**
 * @file    behavioral_binding.h
 * @brief   Parses behavioral source expressions during collection and resolves node-voltage input bindings.
 */

#ifndef NODALYS_COMPILER_BEHAVIORAL_BINDING_H
#define NODALYS_COMPILER_BEHAVIORAL_BINDING_H

#include "compiler/circuit_description.h"
#include "nodalys/types.h"
#include <string>

namespace Nodalys {

// Behavioral source collection
void AppendBehavioralElement(CircuitDescription &description,
                             std::string name,
                             NodeId pos,
                             NodeId neg,
                             const std::string &text,
                             bool is_voltage);

} // namespace Nodalys

#endif // NODALYS_COMPILER_BEHAVIORAL_BINDING_H
