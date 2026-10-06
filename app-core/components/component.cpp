/**
 * @file    component.cpp
 * @brief   Circuit component model implementation.
 */

#include "component.h"

#include <utility>

namespace Core {

/**
 * @brief   Creates a component of the given kind.
 * @param[in] type   Kind of component.
 * @param[in] value  Initial value in the unit given by GetUnit(); ignored when HasValue() is false.
 */
Component::Component(const ComponentType type, const double value) : m_Type(type), m_Value(value) {
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
const char *Component::GetTypeName() const {
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

/**
 * @brief   Returns the SPICE letter that starts the name of this kind of component.
 * @return  "R", "C", "L" or "V", or an empty string for ground, which is not named.
 */
const char *Component::GetNamePrefix() const {
    switch (m_Type) {
    case ComponentType::Resistor:
        return "R";
    case ComponentType::Capacitor:
        return "C";
    case ComponentType::Inductor:
        return "L";
    case ComponentType::VCC:
        // A supply rail is simulated as a voltage source to ground
        return "V";
    case ComponentType::Ground:
        return "";
    }
    return "";
}

/**
 * @brief   Returns the unit of the component value.
 * @return  "Ohm", "F", "H" or "V", or an empty string when the component has no value.
 */
const char *Component::GetUnit() const {
    switch (m_Type) {
    case ComponentType::Resistor:
        return "Ohm";
    case ComponentType::Capacitor:
        return "F";
    case ComponentType::Inductor:
        return "H";
    case ComponentType::VCC:
        return "V";
    case ComponentType::Ground:
        return "";
    }
    return "";
}

/**
 * @brief   Tells whether the component carries a value.
 * @return  False for ground, true for everything else.
 */
bool Component::HasValue() const {
    return m_Type != ComponentType::Ground;
}

/**
 * @brief   Checks whether a value makes sense for this kind of component.
 * @param[in] value  Candidate value in the component unit.
 * @return  True when the value can be simulated: positive for R, C and L, any voltage for a supply.
 */
bool Component::IsValidValue(const double value) const {
    switch (m_Type) {
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
        return value > 0.0;
    case ComponentType::VCC:
        return true;
    case ComponentType::Ground:
        return false;
    }
    return false;
}

/**
 * @brief   Returns the instance name, such as "R1".
 * @return  The name, empty when not assigned yet or for ground.
 */
const std::string &Component::GetName() const {
    return m_Name;
}

/**
 * @brief   Sets the instance name.
 * @param[in] name  New name; it must start with GetNamePrefix() and be unique in the circuit.
 */
void Component::SetName(std::string name) {
    m_Name = std::move(name);
}

/**
 * @brief   Returns the component value.
 * @return  Value in the unit given by GetUnit().
 */
double Component::GetValue() const {
    return m_Value;
}

/**
 * @brief   Sets the component value.
 * @param[in] value  New value in the unit given by GetUnit(); check it with IsValidValue() first.
 */
void Component::SetValue(const double value) {
    m_Value = value;
}

} // namespace Core
