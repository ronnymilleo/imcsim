/**
 * @file    component.cpp
 * @brief   Circuit component model implementation.
 */

#include "component.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>

namespace Core {

namespace {

constexpr auto AllTypes = std::to_array<ComponentType>({
    ComponentType::Resistor,
    ComponentType::Capacitor,
    ComponentType::Inductor,
    ComponentType::Ground,
    ComponentType::VCC,
    ComponentType::VoltageSource,
    ComponentType::CurrentSource,
    ComponentType::Diode,
    ComponentType::ZenerDiode,
    ComponentType::LED,
    ComponentType::NPN,
    ComponentType::PNP,
    ComponentType::NMOS,
    ComponentType::PMOS,
});

} // namespace

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
    return Core::GetTypeName(m_Type);
}

/**
 * @brief   Returns the SPICE letter that starts the name of this kind of component.
 * @return  "R", "C", "L", "V", "Vin", "I", "D", "Q" or "M", or an empty string for ground, which is not named.
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
    case ComponentType::VoltageSource:
        return "Vin";
    case ComponentType::CurrentSource:
        return "I";
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
        return "D";
    case ComponentType::NPN:
    case ComponentType::PNP:
        return "Q";
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return "M";
    case ComponentType::Ground:
        return "";
    }
    return "";
}

/**
 * @brief   Returns the unit of the component value.
 * @return  "Ohm", "F", "H", "V" or "A", or an empty string when the component has no value.
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
    case ComponentType::VoltageSource:
        return "V";
    case ComponentType::CurrentSource:
        return "A";
    case ComponentType::Ground:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return "";
    }
    return "";
}

/**
 * @brief   Tells whether the component carries a numeric value.
 * @return  False for ground and for diodes and transistors, whose model sets their behavior; true for the rest.
 */
bool Component::HasValue() const {
    switch (m_Type) {
    case ComponentType::Ground:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return false;
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
    case ComponentType::VCC:
    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource:
        return true;
    }
    return false;
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
    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource:
        return true;
    case ComponentType::Ground:
    case ComponentType::Diode:
    case ComponentType::ZenerDiode:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
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

/**
 * @brief   Returns a readable name for a kind of component.
 * @param[in] type  Kind of component.
 * @return  The type as text, such as "Resistor"; ParseComponentType() reads it back.
 */
const char *GetTypeName(const ComponentType type) {
    switch (type) {
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
    case ComponentType::VoltageSource:
        return "VoltageSource";
    case ComponentType::CurrentSource:
        return "CurrentSource";
    case ComponentType::Diode:
        return "Diode";
    case ComponentType::ZenerDiode:
        return "ZenerDiode";
    case ComponentType::LED:
        return "LED";
    case ComponentType::NPN:
        return "NPN";
    case ComponentType::PNP:
        return "PNP";
    case ComponentType::NMOS:
        return "NMOS";
    case ComponentType::PMOS:
        return "PMOS";
    }
    return "Unknown";
}

/**
 * @brief   Finds the kind of component named by a text.
 * @param[in] text  A type name as written by GetTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<ComponentType> ParseComponentType(const std::string_view text) {
    for (const ComponentType type : AllTypes) {
        if (text == GetTypeName(type)) {
            return type;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks whether a name can identify a component in a SPICE netlist.
 * @param[in] name    Candidate name, such as "R1" or "Rload".
 * @param[in] prefix  SPICE letter the name must start with, from Component::GetNamePrefix().
 * @return  True when the name starts with the prefix and goes on with at least one letter, digit or underscore.
 */
bool IsValidName(const std::string_view name, const std::string_view prefix) {
    if (prefix.empty() || name.size() <= prefix.size() || !name.starts_with(prefix)) {
        return false;
    }
    return std::ranges::all_of(name.substr(prefix.size()), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    });
}

/**
 * @brief   Builds the next free numbered name for a prefix: R1, R2, R3...
 * @param[in] prefix          SPICE letter of the component kind.
 * @param[in] existing_names  Names already in use, of any kind.
 * @return  The prefix followed by one more than the highest number used with it, so names stay unique even
 *          after deletions. Names that are not the prefix plus a number, such as "Rload", are ignored.
 */
std::string NextComponentName(const std::string_view prefix, const std::vector<std::string> &existing_names) {
    int highest = 0;
    for (const std::string &name : existing_names) {
        if (!name.starts_with(prefix)) {
            continue;
        }
        int number = 0;
        const char *digits_end = name.data() + name.size();
        const auto [end, error] = std::from_chars(name.data() + prefix.size(), digits_end, number);
        if (error == std::errc{} && end == digits_end) {
            highest = std::max(highest, number);
        }
    }
    return std::format("{}{}", prefix, highest + 1);
}

} // namespace Core
