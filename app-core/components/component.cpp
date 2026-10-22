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

constexpr auto ALL_TYPES = std::to_array<ComponentType>({
    ComponentType::RESISTOR,
    ComponentType::CAPACITOR,
    ComponentType::INDUCTOR,
    ComponentType::GROUND,
    ComponentType::VCC,
    ComponentType::VOLTAGE_SOURCE,
    ComponentType::CURRENT_SOURCE,
    ComponentType::DIODE,
    ComponentType::ZENER_DIODE,
    ComponentType::LED,
    ComponentType::NPN,
    ComponentType::PNP,
    ComponentType::NMOS,
    ComponentType::PMOS,
    ComponentType::VCVS,
    ComponentType::VCCS,
    ComponentType::CCCS,
    ComponentType::CCVS,
    ComponentType::OP_AMP,
    ComponentType::NET_LABEL,
    ComponentType::POTENTIOMETER,
    ComponentType::SWITCH,
    ComponentType::PUSH_BUTTON,
    ComponentType::V_SWITCH,
    ComponentType::RELAY,
    ComponentType::TRANSFORMER,
    ComponentType::AND,
    ComponentType::OR,
    ComponentType::NOT,
    ComponentType::NAND,
    ComponentType::NOR,
    ComponentType::XOR,
    ComponentType::XNOR,
    ComponentType::BUFFER,
    ComponentType::D_FLIP_FLOP,
    ComponentType::JK_FLIP_FLOP,
    ComponentType::T_FLIP_FLOP,
    ComponentType::SR_FLIP_FLOP,
    ComponentType::CLOCK,
    ComponentType::LOGIC_INPUT,
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
 * @return  "R", "C", "L", "V", "Vin", "I", "D", "Q", "M", "E", "G", "F", "H" or "U", or an empty string for
 *          ground, which is not named.
 */
const char *Component::GetNamePrefix() const {
    switch (m_Type) {
    case ComponentType::RESISTOR:
        return "R";
    case ComponentType::CAPACITOR:
        return "C";
    case ComponentType::INDUCTOR:
        return "L";
    case ComponentType::VCC:
        // A supply rail is simulated as a voltage source to ground
        return "V";
    case ComponentType::CLOCK:
    case ComponentType::LOGIC_INPUT:
        // A clock and a logic input are voltage sources to ground too
        return "V";
    case ComponentType::VOLTAGE_SOURCE:
        return "Vin";
    case ComponentType::CURRENT_SOURCE:
        return "I";
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED:
        return "D";
    case ComponentType::NPN:
    case ComponentType::PNP:
        return "Q";
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return "M";
    case ComponentType::VCVS:
        return "E";
    case ComponentType::VCCS:
        return "G";
    case ComponentType::CCCS:
        return "F";
    case ComponentType::CCVS:
        return "H";
    case ComponentType::OP_AMP:
    case ComponentType::AND:
    case ComponentType::OR:
    case ComponentType::NOT:
    case ComponentType::NAND:
    case ComponentType::NOR:
    case ComponentType::XOR:
    case ComponentType::XNOR:
    case ComponentType::BUFFER:
    case ComponentType::D_FLIP_FLOP:
    case ComponentType::JK_FLIP_FLOP:
    case ComponentType::T_FLIP_FLOP:
    case ComponentType::SR_FLIP_FLOP:
        return "U";
    case ComponentType::POTENTIOMETER:
        return "RV";
    case ComponentType::SWITCH:
        return "SW";
    case ComponentType::PUSH_BUTTON:
        return "PB";
    case ComponentType::V_SWITCH:
        return "S";
    case ComponentType::RELAY:
        return "K";
    case ComponentType::TRANSFORMER:
        return "T";
    case ComponentType::GROUND:
    case ComponentType::NET_LABEL:
        return "";
    }
    return "";
}

/**
 * @brief   Returns the unit of the component value.
 * @return  "Ohm", "F", "H", "V" or "A"; the gain unit of a controlled source, such as "V/V"; or an empty string
 *          when the component has no value.
 */
const char *Component::GetUnit() const {
    switch (m_Type) {
    case ComponentType::RESISTOR:
    case ComponentType::POTENTIOMETER:
        return "Ohm";
    case ComponentType::CAPACITOR:
        return "F";
    case ComponentType::INDUCTOR:
    case ComponentType::TRANSFORMER:
        return "H";
    case ComponentType::VCC:
    case ComponentType::VOLTAGE_SOURCE:
        return "V";
    case ComponentType::CLOCK:
    case ComponentType::LOGIC_INPUT:
        return "V";
    case ComponentType::CURRENT_SOURCE:
        return "A";
    case ComponentType::VCVS:
        return "V/V";
    case ComponentType::VCCS:
        return "A/V";
    case ComponentType::CCCS:
        return "A/A";
    case ComponentType::CCVS:
        return "V/A";
    case ComponentType::OP_AMP:
    case ComponentType::AND:
    case ComponentType::OR:
    case ComponentType::NOT:
    case ComponentType::NAND:
    case ComponentType::NOR:
    case ComponentType::XOR:
    case ComponentType::XNOR:
    case ComponentType::BUFFER:
    case ComponentType::D_FLIP_FLOP:
    case ComponentType::JK_FLIP_FLOP:
    case ComponentType::T_FLIP_FLOP:
    case ComponentType::SR_FLIP_FLOP:
    case ComponentType::GROUND:
    case ComponentType::NET_LABEL:
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
    case ComponentType::V_SWITCH:
    case ComponentType::RELAY:
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
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
 * @return  False for ground, for diodes and transistors, whose model sets their behavior, and for the ideal op-amp;
 *          true for the rest.
 */
bool Component::HasValue() const {
    switch (m_Type) {
    case ComponentType::GROUND:
    case ComponentType::NET_LABEL:
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
    case ComponentType::V_SWITCH:
    case ComponentType::RELAY:
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
    case ComponentType::OP_AMP:
    case ComponentType::AND:
    case ComponentType::OR:
    case ComponentType::NOT:
    case ComponentType::NAND:
    case ComponentType::NOR:
    case ComponentType::XOR:
    case ComponentType::XNOR:
    case ComponentType::BUFFER:
    case ComponentType::D_FLIP_FLOP:
    case ComponentType::JK_FLIP_FLOP:
    case ComponentType::T_FLIP_FLOP:
    case ComponentType::SR_FLIP_FLOP:
    case ComponentType::CLOCK:
    case ComponentType::LOGIC_INPUT:
        return false;
    case ComponentType::RESISTOR:
    case ComponentType::CAPACITOR:
    case ComponentType::INDUCTOR:
    case ComponentType::POTENTIOMETER:
    case ComponentType::TRANSFORMER:
    case ComponentType::VCC:
    case ComponentType::VOLTAGE_SOURCE:
    case ComponentType::CURRENT_SOURCE:
    case ComponentType::VCVS:
    case ComponentType::VCCS:
    case ComponentType::CCCS:
    case ComponentType::CCVS:
        return true;
    }
    return false;
}

/**
 * @brief   Checks whether a value makes sense for this kind of component.
 * @param[in] value  Candidate value in the component unit.
 * @return  True when the value can be simulated: positive for R, C and L, any voltage for a supply, and any gain,
 *          negative included, for a controlled source.
 */
bool Component::IsValidValue(const double value) const {
    switch (m_Type) {
    case ComponentType::RESISTOR:
    case ComponentType::CAPACITOR:
    case ComponentType::INDUCTOR:
    case ComponentType::POTENTIOMETER:
    case ComponentType::TRANSFORMER:
        return value > 0.0;
    case ComponentType::VCC:
    case ComponentType::VOLTAGE_SOURCE:
    case ComponentType::CURRENT_SOURCE:
    case ComponentType::VCVS:
    case ComponentType::VCCS:
    case ComponentType::CCCS:
    case ComponentType::CCVS:
        return true;
    case ComponentType::GROUND:
    case ComponentType::NET_LABEL:
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
    case ComponentType::V_SWITCH:
    case ComponentType::RELAY:
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED:
    case ComponentType::NPN:
    case ComponentType::PNP:
    case ComponentType::NMOS:
    case ComponentType::PMOS:
    case ComponentType::OP_AMP:
    case ComponentType::AND:
    case ComponentType::OR:
    case ComponentType::NOT:
    case ComponentType::NAND:
    case ComponentType::NOR:
    case ComponentType::XOR:
    case ComponentType::XNOR:
    case ComponentType::BUFFER:
    case ComponentType::D_FLIP_FLOP:
    case ComponentType::JK_FLIP_FLOP:
    case ComponentType::T_FLIP_FLOP:
    case ComponentType::SR_FLIP_FLOP:
    case ComponentType::CLOCK:
    case ComponentType::LOGIC_INPUT:
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
 * @brief   Tells whether this kind of component can start from a value of its own.
 * @return  True for a capacitor, whose initial condition is a voltage, and an inductor, whose is a current; false
 *          for every other kind.
 */
bool Component::SupportsInitialCondition() const {
    return m_Type == ComponentType::CAPACITOR || m_Type == ComponentType::INDUCTOR;
}

/**
 * @brief   Tells whether an initial condition is set.
 * @return  True when SetInitialCondition() has been called and the condition has not been cleared.
 */
bool Component::HasInitialCondition() const {
    return m_InitialCondition.has_value();
}

/**
 * @brief   Returns the initial condition.
 * @return  The voltage a capacitor starts charged to, or the current an inductor starts carrying, or no value when
 *          none is set.
 */
std::optional<double> Component::GetInitialCondition() const {
    return m_InitialCondition;
}

/**
 * @brief   Sets the initial condition.
 * @param[in] value  Voltage for a capacitor or current for an inductor; the sign is free.
 * @note    Only a capacitor or an inductor takes one; the call does nothing for any other kind.
 */
void Component::SetInitialCondition(const double value) {
    if (SupportsInitialCondition()) {
        m_InitialCondition = value;
    }
}

/**
 * @brief   Clears the initial condition.
 * @note    A transient that still has an initial condition elsewhere starts this part uncharged, at zero.
 */
void Component::ClearInitialCondition() {
    m_InitialCondition.reset();
}

/**
 * @brief   Returns a readable name for a kind of component.
 * @param[in] type  Kind of component.
 * @return  The type as text, such as "Resistor"; ParseComponentType() reads it back.
 */
const char *GetTypeName(const ComponentType type) {
    switch (type) {
    case ComponentType::RESISTOR:
        return "Resistor";
    case ComponentType::CAPACITOR:
        return "Capacitor";
    case ComponentType::INDUCTOR:
        return "Inductor";
    case ComponentType::GROUND:
        return "Ground";
    case ComponentType::VCC:
        return "VCC";
    case ComponentType::VOLTAGE_SOURCE:
        return "VoltageSource";
    case ComponentType::CURRENT_SOURCE:
        return "CurrentSource";
    case ComponentType::DIODE:
        return "Diode";
    case ComponentType::ZENER_DIODE:
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
    case ComponentType::VCVS:
        return "VCVS";
    case ComponentType::VCCS:
        return "VCCS";
    case ComponentType::CCCS:
        return "CCCS";
    case ComponentType::CCVS:
        return "CCVS";
    case ComponentType::OP_AMP:
        return "OpAmp";
    case ComponentType::NET_LABEL:
        return "NetLabel";
    case ComponentType::POTENTIOMETER:
        return "Potentiometer";
    case ComponentType::SWITCH:
        return "Switch";
    case ComponentType::PUSH_BUTTON:
        return "PushButton";
    case ComponentType::V_SWITCH:
        return "VSwitch";
    case ComponentType::RELAY:
        return "Relay";
    case ComponentType::TRANSFORMER:
        return "Transformer";
    case ComponentType::AND:
        return "And";
    case ComponentType::OR:
        return "Or";
    case ComponentType::NOT:
        return "Not";
    case ComponentType::NAND:
        return "Nand";
    case ComponentType::NOR:
        return "Nor";
    case ComponentType::XOR:
        return "Xor";
    case ComponentType::XNOR:
        return "Xnor";
    case ComponentType::BUFFER:
        return "Buffer";
    case ComponentType::D_FLIP_FLOP:
        return "DFlipFlop";
    case ComponentType::JK_FLIP_FLOP:
        return "JKFlipFlop";
    case ComponentType::T_FLIP_FLOP:
        return "TFlipFlop";
    case ComponentType::SR_FLIP_FLOP:
        return "SRFlipFlop";
    case ComponentType::CLOCK:
        return "Clock";
    case ComponentType::LOGIC_INPUT:
        return "LogicInput";
    }
    return "Unknown";
}

/**
 * @brief   Finds the kind of component named by a text.
 * @param[in] text  A type name as written by GetTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<ComponentType> ParseComponentType(const std::string_view text) {
    for (const ComponentType type : ALL_TYPES) {
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
