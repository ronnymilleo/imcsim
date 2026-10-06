/**
 * @file    component.h
 * @brief   Circuit component model shared by the simulator core and the GUI.
 */

#ifndef IMCSIM_COMPONENT_H
#define IMCSIM_COMPONENT_H

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
    VCC
};

/**
 * @class   Component
 * @brief   A circuit component in the simulation model.
 * @details Polymorphic base: store derived components through pointers to avoid slicing.
 */
class Component {
public:
    explicit Component(ComponentType type);
    virtual ~Component() = default;

    ComponentType GetType() const;
    const char *GetName() const;

protected:
    ComponentType m_Type{};
};

} // namespace Core

#endif // IMCSIM_COMPONENT_H
