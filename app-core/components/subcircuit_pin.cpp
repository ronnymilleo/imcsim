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

/**
 * @brief   Returns whether the marker is an S-parameter port, and with which number and reference impedance.
 * @return  The port settings; a marker that is no port keeps the numbers it had, so turning it on again restores them.
 */
const PortSettings &SubcircuitPinMarker::GetPort() const {
    return m_Port;
}

/**
 * @brief   Sets the S-parameter port settings of the marker.
 * @param[in] port  New settings; the checks of the analysis tell whether the numbers and impedance can run.
 */
void SubcircuitPinMarker::SetPort(const PortSettings &port) {
    m_Port = port;
}

} // namespace Core
