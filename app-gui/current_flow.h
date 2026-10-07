/**
 * @file    current_flow.h
 * @brief   Works out the current along every piece of wire from the currents of the parts at its ends.
 */

#ifndef IMCSIM_CURRENT_FLOW_H
#define IMCSIM_CURRENT_FLOW_H

#include "helpers.h"
#include "simulator.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @struct  WireCurrent
 * @brief   A straight piece of wire between two connection points, and the current along it.
 * @details Current is positive from Start to End. Wires are split wherever a terminal or another wire end touches
 *          them, since the current changes there.
 */
struct WireCurrent {
    GridPoint Start;
    GridPoint End;
    double Current = 0.0;
};

std::optional<double> GetTerminalCurrent(const Core::Component &component, std::size_t terminal,
                                         const std::vector<Core::ComponentCurrent> &currents);
std::vector<WireCurrent> ComputeWireCurrents(const std::vector<std::unique_ptr<UIElement>> &elements,
                                             const std::vector<UIWire> &wires,
                                             const std::vector<Core::ComponentCurrent> &currents);

} // namespace GUI

#endif // IMCSIM_CURRENT_FLOW_H
