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
};

/**
 * @class   Component
 * @brief   A circuit component in the simulation model.
 * @details Polymorphic base: store derived components through pointers to avoid slicing. Names follow SPICE,
 *          where the first letter tells the element kind (R1, C1, L1, V1). Ground has no name and no value.
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

protected:
    ComponentType m_Type{};
    std::string m_Name;
    double m_Value = 0.0;
};

// Type names
const char *GetTypeName(ComponentType type);
std::optional<ComponentType> ParseComponentType(std::string_view text);

// Instance names
bool IsValidName(std::string_view name, std::string_view prefix);
std::string NextComponentName(std::string_view prefix, const std::vector<std::string> &existing_names);

} // namespace Core

#endif // IMCSIM_COMPONENT_H
