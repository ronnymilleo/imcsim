/**
 * @file    component.cpp
 * @brief   Circuit component model implementation.
 */

#include "circuit/components/component.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace Core {

namespace {

/**
 * @enum    ValueRule
 * @brief   Which values a kind of component accepts.
 */
enum class ValueRule {
    /// The component has no value, so no value is valid.
    NONE,
    /// Any number, negative and not-a-number included.
    ANY,
    /// Strictly above zero, as for a resistance, a capacitance or an inductance.
    POSITIVE,
    /// Any finite number.
    FINITE,
};

/**
 * @struct  ComponentTraits
 * @brief   What is fixed for a kind of component, whatever its instance.
 * @details One row per ComponentType in COMPONENT_TRAITS, so a new type is described in one place.
 */
struct ComponentTraits {
    ComponentType Type;
    /// Text written to files, read back by ParseComponentType(). All traits strings are literals, so they end in a
    /// null character and can be returned as C strings.
    std::string_view TypeName;
    /// SPICE letter that starts the instance name; empty when the kind is not named.
    std::string_view NamePrefix;
    /// Unit of the value, or of the gain for a controlled source; empty when there is none.
    std::string_view Unit;
    /// Rule for the value; a component has a value exactly when the rule is not NONE.
    ValueRule AcceptedValues;
};

constexpr auto COMPONENT_TRAITS = std::to_array<ComponentTraits>({
    {
        .Type = ComponentType::RESISTOR,
        .TypeName = "Resistor",
        .NamePrefix = "R",
        .Unit = "Ohm",
        .AcceptedValues = ValueRule::POSITIVE,
    },
    {
        .Type = ComponentType::CAPACITOR,
        .TypeName = "Capacitor",
        .NamePrefix = "C",
        .Unit = "F",
        .AcceptedValues = ValueRule::POSITIVE,
    },
    {
        .Type = ComponentType::INDUCTOR,
        .TypeName = "Inductor",
        .NamePrefix = "L",
        .Unit = "H",
        .AcceptedValues = ValueRule::POSITIVE,
    },
    {
        .Type = ComponentType::GROUND,
        .TypeName = "Ground",
        .NamePrefix = "",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    // A supply rail, a clock and a logic input are simulated as voltage sources to ground
    {
        .Type = ComponentType::VCC,
        .TypeName = "VCC",
        .NamePrefix = "V",
        .Unit = "V",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::VOLTAGE_SOURCE,
        .TypeName = "VoltageSource",
        .NamePrefix = "Vin",
        .Unit = "V",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::CURRENT_SOURCE,
        .TypeName = "CurrentSource",
        .NamePrefix = "I",
        .Unit = "A",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::DIODE,
        .TypeName = "Diode",
        .NamePrefix = "D",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::ZENER_DIODE,
        .TypeName = "ZenerDiode",
        .NamePrefix = "D",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::LED,
        .TypeName = "LED",
        .NamePrefix = "D",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NPN,
        .TypeName = "NPN",
        .NamePrefix = "Q",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::PNP,
        .TypeName = "PNP",
        .NamePrefix = "Q",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NMOS,
        .TypeName = "NMOS",
        .NamePrefix = "M",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::PMOS,
        .TypeName = "PMOS",
        .NamePrefix = "M",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::VCVS,
        .TypeName = "VCVS",
        .NamePrefix = "E",
        .Unit = "V/V",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::VCCS,
        .TypeName = "VCCS",
        .NamePrefix = "G",
        .Unit = "A/V",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::CCCS,
        .TypeName = "CCCS",
        .NamePrefix = "F",
        .Unit = "A/A",
        .AcceptedValues = ValueRule::ANY,
    },
    {
        .Type = ComponentType::CCVS,
        .TypeName = "CCVS",
        .NamePrefix = "H",
        .Unit = "V/A",
        .AcceptedValues = ValueRule::ANY,
    },
    // The expression sets the voltage, so the part has a unit but no value
    {
        .Type = ComponentType::BEHAVIORAL_VOLTAGE,
        .TypeName = "BehavioralVoltage",
        .NamePrefix = "B",
        .Unit = "V",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::BEHAVIORAL_CURRENT,
        .TypeName = "BehavioralCurrent",
        .NamePrefix = "B",
        .Unit = "A",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::OP_AMP,
        .TypeName = "OpAmp",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::SUBCIRCUIT,
        .TypeName = "Subcircuit",
        .NamePrefix = "X",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NET_LABEL,
        .TypeName = "NetLabel",
        .NamePrefix = "",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::INITIAL_VOLTAGE,
        .TypeName = "InitialVoltage",
        .NamePrefix = "",
        .Unit = "V",
        .AcceptedValues = ValueRule::FINITE,
    },
    {
        .Type = ComponentType::NODESET,
        .TypeName = "Nodeset",
        .NamePrefix = "",
        .Unit = "V",
        .AcceptedValues = ValueRule::FINITE,
    },
    {
        .Type = ComponentType::PIN,
        .TypeName = "Pin",
        .NamePrefix = "",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::POTENTIOMETER,
        .TypeName = "Potentiometer",
        .NamePrefix = "RV",
        .Unit = "Ohm",
        .AcceptedValues = ValueRule::POSITIVE,
    },
    {
        .Type = ComponentType::SWITCH,
        .TypeName = "Switch",
        .NamePrefix = "SW",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::PUSH_BUTTON,
        .TypeName = "PushButton",
        .NamePrefix = "PB",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::V_SWITCH,
        .TypeName = "VSwitch",
        .NamePrefix = "S",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::RELAY,
        .TypeName = "Relay",
        .NamePrefix = "K",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::FUSE,
        .TypeName = "Fuse",
        .NamePrefix = "FU",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::TRANSFORMER,
        .TypeName = "Transformer",
        .NamePrefix = "T",
        .Unit = "H",
        .AcceptedValues = ValueRule::POSITIVE,
    },
    {
        .Type = ComponentType::AND,
        .TypeName = "And",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::OR,
        .TypeName = "Or",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NOT,
        .TypeName = "Not",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NAND,
        .TypeName = "Nand",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::NOR,
        .TypeName = "Nor",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::XOR,
        .TypeName = "Xor",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::XNOR,
        .TypeName = "Xnor",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::BUFFER,
        .TypeName = "Buffer",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::D_FLIP_FLOP,
        .TypeName = "DFlipFlop",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::JK_FLIP_FLOP,
        .TypeName = "JKFlipFlop",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::T_FLIP_FLOP,
        .TypeName = "TFlipFlop",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::SR_FLIP_FLOP,
        .TypeName = "SRFlipFlop",
        .NamePrefix = "U",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    // Simulated as a voltage source to ground, like a supply rail
    {
        .Type = ComponentType::CLOCK,
        .TypeName = "Clock",
        .NamePrefix = "V",
        .Unit = "V",
        .AcceptedValues = ValueRule::NONE,
    },
    // Simulated as a voltage source to ground, like a supply rail
    {
        .Type = ComponentType::LOGIC_INPUT,
        .TypeName = "LogicInput",
        .NamePrefix = "V",
        .Unit = "V",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::LOGIC_OUTPUT,
        .TypeName = "LogicOutput",
        .NamePrefix = "",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
    {
        .Type = ComponentType::SEVEN_SEGMENT,
        .TypeName = "SevenSegment",
        .NamePrefix = "",
        .Unit = "",
        .AcceptedValues = ValueRule::NONE,
    },
});

/**
 * @brief   Checks that the table has one row per type, in the order of the enum.
 * @return  True when the row at each index describes the type with that index and no type is left out.
 */
constexpr bool HasOneRowPerType() {
    if (COMPONENT_TRAITS.size() != static_cast<std::size_t>(LAST_COMPONENT_TYPE) + 1) {
        return false;
    }
    for (std::size_t index = 0; index < COMPONENT_TRAITS.size(); ++index) {
        if (static_cast<std::size_t>(COMPONENT_TRAITS[index].Type) != index) {
            return false;
        }
    }
    return true;
}

static_assert(HasOneRowPerType(), "COMPONENT_TRAITS needs one row per ComponentType, in the order of the enum");

/**
 * @brief   Looks up the row of a kind of component.
 * @param[in] type  Kind of component.
 * @return  The row of the type.
 */
constexpr const ComponentTraits &GetTraits(const ComponentType type) {
    return COMPONENT_TRAITS[static_cast<std::size_t>(type)];
}

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
 * @return  "R", "C", "L", "V", "Vin", "I", "D", "Q", "M", "E", "G", "F", "H", "U" or "X", or an empty string
 *          for ground, net labels and subcircuit pin markers, which are not named.
 */
const char *Component::GetNamePrefix() const {
    return GetTraits(m_Type).NamePrefix.data();
}

/**
 * @brief   Returns the unit of the component value.
 * @return  "Ohm", "F", "H", "V" or "A"; the gain unit of a controlled source, such as "V/V"; or an empty string
 *          when the component has no value.
 */
const char *Component::GetUnit() const {
    return GetTraits(m_Type).Unit.data();
}

/**
 * @brief   Tells whether the component carries a numeric value.
 * @return  False for ground, for diodes and transistors, whose model sets their behavior, and for the ideal op-amp;
 *          true for the rest.
 */
bool Component::HasValue() const {
    return GetTraits(m_Type).AcceptedValues != ValueRule::NONE;
}

/**
 * @brief   Checks whether a value makes sense for this kind of component.
 * @param[in] value  Candidate value in the component unit.
 * @return  True when the value can be simulated: positive for R, C and L, any voltage for a supply or an initial
 *          voltage, and any gain, negative included, for a controlled source.
 */
bool Component::IsValidValue(const double value) const {
    switch (GetTraits(m_Type).AcceptedValues) {
    case ValueRule::ANY:
        return true;
    case ValueRule::POSITIVE:
        return value > 0.0;
    case ValueRule::FINITE:
        return std::isfinite(value);
    case ValueRule::NONE:
    default:
        return false;
    }
}

/**
 * @brief   Tells whether the value can follow a parameter of the subcircuit the part belongs to.
 * @return  True for resistors, capacitors and inductors, whose netlist line holds the value as it is; false for the
 *          parts that work their lines out from the value, such as a potentiometer or a transformer.
 */
bool Component::CanFollowParameter() const {
    return m_Type == ComponentType::RESISTOR || m_Type == ComponentType::CAPACITOR || m_Type == ComponentType::INDUCTOR;
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
    m_ValueExpression.reset();
}

/**
 * @brief   Returns the parameter the value refers to, for a part inside a subcircuit.
 * @return  An expression such as "{R}", written in the netlist instead of the value; no value for a fixed value.
 */
const std::optional<std::string> &Component::GetValueExpression() const {
    return m_ValueExpression;
}

/**
 * @brief   Makes the value follow a parameter of the subcircuit the part belongs to.
 * @param[in] expression  A parameter name in braces, such as "{R}".
 * @note    Parts that cannot follow a parameter (CanFollowParameter()) ignore it. SetValue() goes back to a fixed
 * value.
 */
void Component::SetValueExpression(std::string expression) {
    if (CanFollowParameter()) {
        m_ValueExpression = std::move(expression);
    }
}

/**
 * @brief   Goes back to the fixed value, which GetValue() returns.
 */
void Component::ClearValueExpression() {
    m_ValueExpression.reset();
}

/**
 * @brief   Tells whether the part can use a model from a library.
 * @return  True for diodes, LEDs, Zeners and transistors.
 */
bool Component::TakesLibraryModel() const {
    static constexpr auto MODELED_TYPES = std::to_array({ComponentType::DIODE,
                                                         ComponentType::ZENER_DIODE,
                                                         ComponentType::LED,
                                                         ComponentType::NPN,
                                                         ComponentType::PNP,
                                                         ComponentType::NMOS,
                                                         ComponentType::PMOS});
    return std::ranges::contains(MODELED_TYPES, m_Type);
}

/**
 * @brief   Tells whether the part uses a model from a library.
 * @return  True when it names one, in place of a ready model or custom parameters.
 */
bool Component::HasLibraryModel() const {
    return !m_LibraryModel.empty();
}

/**
 * @brief   Returns the name of the library model the part uses.
 * @return  The name, as its library writes it, or an empty string when the part uses a ready or custom model.
 */
const std::string &Component::GetLibraryModel() const {
    return m_LibraryModel;
}

/**
 * @brief   Makes the part use a model from a library, which the schematic keeps.
 * @param[in] name  Name of the model; a part that cannot use one (TakesLibraryModel()) ignores it.
 * @note    Choosing a ready model or custom parameters later goes back to them.
 */
void Component::SetLibraryModel(std::string name) {
    if (TakesLibraryModel()) {
        m_LibraryModel = std::move(name);
    }
}

/**
 * @brief   Stops using a library model, so the part uses its ready model or its custom parameters again.
 */
void Component::ClearLibraryModel() {
    m_LibraryModel.clear();
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
 * @brief   Tells whether this kind of component can have a tolerance.
 * @return  True for a resistor, capacitor, inductor and voltage source; a source only varies while it is a DC one.
 */
bool Component::SupportsTolerance() const {
    return m_Type == ComponentType::RESISTOR || m_Type == ComponentType::CAPACITOR || m_Type == ComponentType::INDUCTOR
        || m_Type == ComponentType::VOLTAGE_SOURCE;
}

/**
 * @brief   Tells whether a tolerance is set.
 * @return  True when SetTolerance() has been called and the tolerance has not been cleared.
 */
bool Component::HasTolerance() const {
    return m_Tolerance.has_value();
}

/**
 * @brief   Returns the tolerance.
 * @return  The percent the value may be off in a Monte Carlo run, or no value when the part does not vary.
 */
std::optional<double> Component::GetTolerance() const {
    return m_Tolerance;
}

/**
 * @brief   Sets the tolerance.
 * @param[in] percent  Percent the value may be off, above 0 and up to MAX_TOLERANCE_PERCENT.
 * @note    Only a part that SupportsTolerance() takes one, and only a valid one; the call does nothing otherwise.
 */
void Component::SetTolerance(const double percent) {
    if (SupportsTolerance() && percent > 0.0 && percent <= MAX_TOLERANCE_PERCENT) {
        m_Tolerance = percent;
    }
}

/**
 * @brief   Clears the tolerance, so the part keeps its value in every Monte Carlo run.
 */
void Component::ClearTolerance() {
    m_Tolerance.reset();
}

/**
 * @brief   Tells whether a kind of part is the pin marker of a subcircuit.
 * @param[in] type  Kind of part.
 * @return  True for ComponentType::PIN.
 */
bool IsSubcircuitPin(const ComponentType type) {
    return type == ComponentType::PIN;
}

/**
 * @brief   Tells whether a kind of part only shows the logic levels of its nodes, with no netlist line of its own.
 * @param[in] type  Kind of part.
 * @return  True for the logic output and the 7-segment display.
 */
bool IsLogicDisplay(const ComponentType type) {
    return type == ComponentType::LOGIC_OUTPUT || type == ComponentType::SEVEN_SEGMENT;
}

/**
 * @brief   Returns a readable name for a kind of component.
 * @param[in] type  Kind of component.
 * @return  The type as text, such as "Resistor"; ParseComponentType() reads it back.
 */
const char *GetTypeName(const ComponentType type) {
    return GetTraits(type).TypeName.data();
}

/**
 * @brief   Finds the kind of component named by a text.
 * @param[in] text  A type name as written by GetTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<ComponentType> ParseComponentType(const std::string_view text) {
    for (const ComponentTraits &traits : COMPONENT_TRAITS) {
        if (text == traits.TypeName) {
            return traits.Type;
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
