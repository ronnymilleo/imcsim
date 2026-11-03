/**
 * @file    subcircuit_pins.h
 * @brief   Checks the pins of a block definition, tells the edge each one goes on and hands them to its instances.
 */

#ifndef IMCSIM_SUBCIRCUIT_PINS_H
#define IMCSIM_SUBCIRCUIT_PINS_H

#include "components/subcircuit.h"
#include "connectivity.h"
#include "schematic_file.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// Checks
std::optional<std::string> CheckPinNodes(const Connectivity &connectivity, const std::vector<SavedSubcircuitPin> &pins);

// Sides
Core::PinSide GetMarkerSide(const UIElement &marker);
std::optional<Core::PinSide> FindMarkerSide(const std::vector<std::unique_ptr<UIElement>> &elements, GridPoint point);
const char *GetPinSideName(Core::PinSide side);

// Instances
void ApplyDefinitionPins(Core::SubcircuitInstance &instance, const SavedSubcircuit &definition);

} // namespace GUI

#endif // IMCSIM_SUBCIRCUIT_PINS_H
