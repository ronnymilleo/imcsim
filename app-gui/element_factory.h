/**
 * @file    element_factory.h
 * @brief   Creates the schematic element that matches a component type.
 */

#ifndef IMCSIM_ELEMENT_FACTORY_H
#define IMCSIM_ELEMENT_FACTORY_H

#include "components/component.h"
#include "helpers.h"
#include "ui_elements/ui_element.h"
#include <memory>

namespace GUI {

std::unique_ptr<UIElement> CreateElement(Core::ComponentType type, GridPoint position, Rotation rotation);

} // namespace GUI

#endif // IMCSIM_ELEMENT_FACTORY_H
