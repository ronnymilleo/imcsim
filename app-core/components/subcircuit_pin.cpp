/**
 * @file    subcircuit_pin.cpp
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#include "components/subcircuit_pin.h"

namespace Core {

/**
 * @brief   Creates an unnamed marker.
 */
SubcircuitPinMarker::SubcircuitPinMarker() : NetLabel(ComponentType::PIN) {
}

/**
 * @brief   Copies the marker, with its name.
 * @return  The copy.
 */
std::unique_ptr<Component> SubcircuitPinMarker::Clone() const {
    return std::make_unique<SubcircuitPinMarker>(*this);
}

} // namespace Core
