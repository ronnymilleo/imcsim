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

} // namespace Core
