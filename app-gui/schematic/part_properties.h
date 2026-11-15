/**
 * @file    part_properties.h
 * @brief   Which fields the properties of a part have: a value, a model, parameters, a name or a state.
 */

#ifndef IMCSIM_PART_PROPERTIES_H
#define IMCSIM_PART_PROPERTIES_H

#include "circuit/components/component.h"

namespace GUI {

// Fields
bool HasPropertyFields(const Core::Component &component);
bool HasModelChoice(Core::ComponentType type);
bool ShowsValueFields(const Core::Component &component);

// Typing
bool CanStartTypingValue(const Core::Component &component);

} // namespace GUI

#endif // IMCSIM_PART_PROPERTIES_H
