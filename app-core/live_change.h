/**
 * @file    live_change.h
 * @brief   What a live transient changes in the circuit it runs when the user flips a switch, presses a button, sets
 *          a logic input or turns a potentiometer.
 */

#ifndef IMCSIM_LIVE_CHANGE_H
#define IMCSIM_LIVE_CHANGE_H

#include "components/component.h"
#include <string>
#include <vector>

namespace Core {

/**
 * @enum    LiveQuantity
 * @brief   The figure of a netlist element that a live change sets.
 */
enum class LiveQuantity {
    RESISTANCE,
    DC_VOLTAGE
};

/**
 * @struct  LiveChange
 * @brief   One element of the netlist and the new value of one of its figures, as a backend applies it to a run in
 *          progress.
 * @details The element is named as the netlist writes it (Circuit::ToSpiceNetlist()), such as "R-SW1" for the resistor
 *          of a switch, so the description does not depend on the backend that applies it.
 */
struct LiveChange {
    std::string Element;
    LiveQuantity Quantity = LiveQuantity::RESISTANCE;
    double Value = 0.0;

    bool operator==(const LiveChange &) const = default;
};

// Changes
bool IsLiveChangeable(ComponentType type);
std::vector<LiveChange> DescribeLiveChange(const Component &component);

} // namespace Core

#endif // IMCSIM_LIVE_CHANGE_H
