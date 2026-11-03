/**
 * @file    subcircuit_pins.h
 * @brief   Checks the pins of a block definition and hands them to its instances.
 */

#ifndef IMCSIM_SUBCIRCUIT_PINS_H
#define IMCSIM_SUBCIRCUIT_PINS_H

#include "components/subcircuit.h"
#include "connectivity.h"
#include "schematic_file.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// Checks
std::optional<std::string> CheckPinNodes(const Connectivity &connectivity, const std::vector<SavedSubcircuitPin> &pins);

// Instances
void ApplyDefinitionPins(Core::SubcircuitInstance &instance, const SavedSubcircuit &definition);

} // namespace GUI

#endif // IMCSIM_SUBCIRCUIT_PINS_H
