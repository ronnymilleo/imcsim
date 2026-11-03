/**
 * @file    subcircuit_pin.cpp
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#include "subcircuit_pin.h"

namespace Core {

/**
 * @brief   Creates an unnamed marker.
 * @param[in] type  ComponentType::PIN for an input or ComponentType::POUT for an output.
 */
SubcircuitPinMarker::SubcircuitPinMarker(const ComponentType type) : NetLabel(type) {
}

/**
 * @brief   Copies the marker, with its name.
 * @return  The copy.
 */
std::unique_ptr<Component> SubcircuitPinMarker::Clone() const {
    return std::make_unique<SubcircuitPinMarker>(*this);
}

} // namespace Core
