/**
 * @file    element_factory.h
 * @brief   Creates the schematic element that matches a component type, and names each kind of part for the user.
 */

#ifndef IMCSIM_ELEMENT_FACTORY_H
#define IMCSIM_ELEMENT_FACTORY_H

#include "components/component.h"
#include "helpers.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <string_view>
#include <vector>

namespace GUI {

std::unique_ptr<UIElement> CreateElement(Core::ComponentType type, GridPoint position, Rotation rotation);

// Part names, as menus, tooltips and the part search show them
const char *GetPartName(Core::ComponentType type);
std::vector<Core::ComponentType> FindParts(std::string_view query);

} // namespace GUI

#endif // IMCSIM_ELEMENT_FACTORY_H
