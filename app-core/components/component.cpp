/**
 * @file    component.cpp
 * @brief   Circuit component model implementation.
 */

#include "component.h"

namespace Core {

/**
 * @brief   Creates a component of the given kind.
 * @param[in] type  Kind of component.
 */
Component::Component(const ComponentType type) : m_Type(type) {
}

/**
 * @brief   Returns the kind of this component.
 * @return  The component type.
 */
ComponentType Component::GetType() const {
    return m_Type;
}

/**
 * @brief   Returns a readable name for the kind of this component.
 * @return  The component type as text, such as "Resistor".
 */
const char *Component::GetName() const {
    switch (m_Type) {
    case ComponentType::Resistor:
        return "Resistor";
    case ComponentType::Capacitor:
        return "Capacitor";
    case ComponentType::Inductor:
        return "Inductor";
    case ComponentType::Ground:
        return "Ground";
    case ComponentType::VCC:
        return "VCC";
    }
    return "Unknown";
}

} // namespace Core
