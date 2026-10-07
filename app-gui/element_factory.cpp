/**
 * @file    element_factory.cpp
 * @brief   Creates the schematic element that matches a component type.
 */

#include "element_factory.h"

#include "ui_elements/ui_bjt.h"
#include "ui_elements/ui_capacitor.h"
#include "ui_elements/ui_current_source.h"
#include "ui_elements/ui_diode.h"
#include "ui_elements/ui_ground.h"
#include "ui_elements/ui_inductor.h"
#include "ui_elements/ui_mosfet.h"
#include "ui_elements/ui_resistor.h"
#include "ui_elements/ui_vcc.h"
#include "ui_elements/ui_voltage_source.h"

namespace GUI {

/**
 * @brief   Creates an element, with its simulation component, for a component type.
 * @param[in] type      Kind of component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 * @return  The new element; its component starts unnamed and with the default value.
 */
std::unique_ptr<UIElement> CreateElement(const Core::ComponentType type, const GridPoint position,
                                         const Rotation rotation) {
    switch (type) {
    case Core::ComponentType::Resistor:
        return std::make_unique<UIResistor>(position, rotation);
    case Core::ComponentType::Capacitor:
        return std::make_unique<UICapacitor>(position, rotation);
    case Core::ComponentType::Inductor:
        return std::make_unique<UIInductor>(position, rotation);
    case Core::ComponentType::Ground:
        return std::make_unique<UIGround>(position, rotation);
    case Core::ComponentType::VCC:
        return std::make_unique<UIVCC>(position, rotation);
    case Core::ComponentType::VoltageSource:
        return std::make_unique<UIVoltageSource>(position, rotation);
    case Core::ComponentType::CurrentSource:
        return std::make_unique<UICurrentSource>(position, rotation);
    case Core::ComponentType::Diode:
    case Core::ComponentType::ZenerDiode:
    case Core::ComponentType::LED:
        return std::make_unique<UIDiode>(type, position, rotation);
    case Core::ComponentType::NPN:
    case Core::ComponentType::PNP:
        return std::make_unique<UIBJT>(type, position, rotation);
    case Core::ComponentType::NMOS:
    case Core::ComponentType::PMOS:
        return std::make_unique<UIMOSFET>(type, position, rotation);
    }
    return nullptr;
}

} // namespace GUI
