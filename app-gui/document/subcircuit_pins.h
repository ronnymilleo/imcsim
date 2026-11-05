/**
 * @file    subcircuit_pins.h
 * @brief   Checks the pins of a block definition, tells the edge each one goes on and hands them to its instances.
 */

#ifndef IMCSIM_SUBCIRCUIT_PINS_H
#define IMCSIM_SUBCIRCUIT_PINS_H

#include "components/component.h"
#include "components/subcircuit.h"
#include "document/schematic_file.h"
#include "editing/connectivity.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/// Every edge of a block's box, in the order the dialogs offer them.
inline constexpr auto PIN_SIDES =
    std::to_array({Core::PinSide::LEFT, Core::PinSide::RIGHT, Core::PinSide::TOP, Core::PinSide::BOTTOM});

// Checks
std::optional<std::string> CheckPinNodes(const Connectivity &connectivity, const std::vector<SavedSubcircuitPin> &pins);

// Sides
Core::PinSide GetMarkerSide(const UIElement &marker);
std::optional<Core::PinSide> FindMarkerSide(const std::vector<std::unique_ptr<UIElement>> &elements, GridPoint point);
Rotation GetSideRotation(Core::PinSide side);
const char *GetPinSideName(Core::PinSide side);
std::optional<Core::PinSide> ParsePinSide(std::string_view name);

// S-parameter ports
bool IsPortNumberTaken(const std::vector<std::unique_ptr<UIElement>> &elements, int number,
                       const Core::Component *except);
int FindFreePortNumber(const std::vector<std::unique_ptr<UIElement>> &elements, const Core::Component *except);

// Instances
void ApplyDefinitionPins(Core::SubcircuitInstance &instance, const SavedSubcircuit &definition);

} // namespace GUI

#endif // IMCSIM_SUBCIRCUIT_PINS_H
