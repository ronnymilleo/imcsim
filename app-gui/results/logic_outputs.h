/**
 * @file    logic_outputs.h
 * @brief   Reads the logic levels the logic outputs and 7-segment displays show from the result on display.
 */

#ifndef IMCSIM_LOGIC_OUTPUTS_H
#define IMCSIM_LOGIC_OUTPUTS_H

#include "results/logic_trace.h"
#include "schematic/connectivity.h"
#include "simulation/simulation_results.h"
#include "ui_elements/ui_element.h"
#include <optional>
#include <vector>

namespace GUI {

// Reading
std::optional<LogicState> ReadLogicOutput(const UIElement &output, const Connectivity &connectivity,
                                          const Core::OperatingPoint &point);
std::vector<std::optional<LogicState>> ReadLogicDisplay(const UIElement &display, const Connectivity &connectivity,
                                                        const Core::OperatingPoint &point);

} // namespace GUI

#endif // IMCSIM_LOGIC_OUTPUTS_H
