/**
 * @file    part_heat.h
 * @brief   What the heat color of a part reads: the largest current through its terminals or the power it absorbs.
 */

#ifndef IMCSIM_PART_HEAT_H
#define IMCSIM_PART_HEAT_H

#include "circuit/components/component.h"
#include "results/circuit_snapshot.h"
#include "simulation/simulation_results.h"
#include "ui_elements/ui_element.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

// What a part is colored by
bool CarriesCurrentOrPower(Core::ComponentType type);
double GetLargestTerminalCurrent(const UIElement &element, const std::vector<Core::ComponentCurrent> &currents);
std::optional<ComponentPower> FindPower(const std::vector<ComponentPower> &powers, const std::string &name);

} // namespace GUI

#endif // IMCSIM_PART_HEAT_H
