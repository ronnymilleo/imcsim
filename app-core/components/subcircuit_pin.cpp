/**
 * @file    subcircuit_pin.cpp
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#include "subcircuit_pin.h"

namespace Core {

SubcircuitPinMarker::SubcircuitPinMarker(const ComponentType type) : NetLabel(type) {
}

std::unique_ptr<Component> SubcircuitPinMarker::Clone() const {
    return std::make_unique<SubcircuitPinMarker>(*this);
}

} // namespace Core
