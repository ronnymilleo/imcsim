/**
 * @file    component.h
 * @brief   Circuit component model shared by the simulator core and the GUI.
 */

#ifndef IMCSIM_COMPONENT_H
#define IMCSIM_COMPONENT_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @enum    ComponentType
 * @brief   Kinds of circuit components the simulator supports.
 */
enum class ComponentType {
    Resistor,
    Capacitor,
    Inductor,
    Ground,
    VCC,
    VoltageSource,
    CurrentSource,
    Diode,
    ZenerDiode,
    LED,
    NPN,
    PNP,
    NMOS,
    PMOS,
    VCVS,
    VCCS,
    CCCS,
    CCVS,
    OpAmp,
    NetLabel,
    Potentiometer,
    Switch,
    PushButton,
    VSwitch,
    Relay,
    Transformer,
    And,
    Or,
    Not,
    Nand,
    Nor,
    Xor,
    Xnor,
    Buffer,
    DFlipFlop,
    JKFlipFlop,
    TFlipFlop,
    SRFlipFlop,
};

/**
 * @class   Component
 * @brief   A circuit component in the simulation model.
 * @details Polymorphic base: store derived components through pointers to avoid slicing. Names follow SPICE,
 *          where the first letter tells the element kind (R1, C1, L1, V1, I1, D1, Q1, M1, E1, G1, F1, H1); op-amps
 *          are named U1, U2... Ground has no name and no value, diodes and transistors have a model instead of a
 *          value, and controlled sources have a gain as their value. A potentiometer is RV1, RV2..., its value the
 *          resistance between its ends; a switch is SW1 and a push button PB1, with no value;
 *          a voltage-controlled switch is S1 and a relay K1, with no value either;
 *          a transformer is T1, its value the inductance of its primary;
 *          logic gates and flip-flops are U1, U2... like op-amps, with no value.
 */
class Component {
public:
    Component(ComponentType type, double value);
    virtual ~Component() = default;

    // Kind of component
    ComponentType GetType() const;
    const char *GetTypeName() const;
    const char *GetNamePrefix() const;
    const char *GetUnit() const;
    bool HasValue() const;
    bool IsValidValue(double value) const;

    // Instance data
    const std::string &GetName() const;
    void SetName(std::string name);
    double GetValue() const;
    void SetValue(double value);

    // Initial condition
    bool SupportsInitialCondition() const;
    bool HasInitialCondition() const;
    std::optional<double> GetInitialCondition() const;
    void SetInitialCondition(double value);
    void ClearInitialCondition();

protected:
    ComponentType m_Type{};
    std::string m_Name;
    double m_Value = 0.0;
    // Voltage a capacitor starts charged to or current an inductor starts carrying; only these kinds carry one
    std::optional<double> m_InitialCondition;
};

// Type names
const char *GetTypeName(ComponentType type);
std::optional<ComponentType> ParseComponentType(std::string_view text);

// Instance names
bool IsValidName(std::string_view name, std::string_view prefix);
std::string NextComponentName(std::string_view prefix, const std::vector<std::string> &existing_names);

} // namespace Core

#endif // IMCSIM_COMPONENT_H
