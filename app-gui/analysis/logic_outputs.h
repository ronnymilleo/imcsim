/**
 * @file    logic_outputs.h
 * @brief   Reads the logic level a logic output shows from the result on display.
 */

#ifndef IMCSIM_LOGIC_OUTPUTS_H
#define IMCSIM_LOGIC_OUTPUTS_H

#include "editing/connectivity.h"
#include "plots/logic_trace.h"
#include "simulation/simulation_results.h"
#include "ui_elements/ui_element.h"
#include <optional>

namespace GUI {

// Reading
std::optional<LogicState> ReadLogicOutput(const UIElement &output, const Connectivity &connectivity,
                                          const Core::OperatingPoint &point);

} // namespace GUI

#endif // IMCSIM_LOGIC_OUTPUTS_H
