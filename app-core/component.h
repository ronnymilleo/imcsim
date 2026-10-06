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
    Capacitor
};

/**
 * @class   Component
 * @brief   A circuit component in the simulation model.
 */
class Component {
public:
    Component() = default;
    ~Component() = default;

private:
};

} // namespace Core

#endif // IMCSIM_COMPONENT_H
