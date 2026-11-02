/**
 * @file    live_change.cpp
 * @brief   What a live transient changes in the circuit it runs when the user flips a switch, presses a button, sets
 *          a logic input or turns a potentiometer.
 */

#include "live_change.h"

#include "components/logic_input.h"
#include "components/potentiometer.h"
#include "components/switch.h"
#include <format>

namespace Core {

/**
 * @brief   Tells whether a kind of part can change while a live transient runs.
 * @param[in] type  Kind of component.
 * @return  True for switches, push buttons, logic inputs and potentiometers, which the user sets by hand.
 */
bool IsLiveChangeable(const ComponentType type) {
    return IsSwitch(type) || type == ComponentType::LOGIC_INPUT || type == ComponentType::POTENTIOMETER;
}

/**
 * @brief   Describes the state of a part as the changes that bring a run in progress to it.
 * @param[in] component  Part whose state the run should take, as it is now.
 * @return  The resistance of a switch or push button, the voltage of a logic input or the two halves of the track of a
 *          potentiometer, with the values the netlist writes for that state; nothing for any other part.
 */
std::vector<LiveChange> DescribeLiveChange(const Component &component) {
    const std::string &name = component.GetName();
    const ComponentType type = component.GetType();
    if (IsSwitch(type)) {
        const auto &contact = static_cast<const Switch &>(component);
        return {{.Element = std::format("R-{}", name),
                 .Quantity = LiveQuantity::RESISTANCE,
                 .Value = contact.GetResistance()}};
    }
    if (type == ComponentType::LOGIC_INPUT) {
        const auto &input = static_cast<const LogicInput &>(component);
        return {{.Element = name, .Quantity = LiveQuantity::DC_VOLTAGE, .Value = input.GetVoltage()}};
    }
    if (type == ComponentType::POTENTIOMETER) {
        const auto &pot = static_cast<const Potentiometer &>(component);
        return {{.Element = std::format("R-{}a", name),
                 .Quantity = LiveQuantity::RESISTANCE,
                 .Value = pot.GetFirstTrackResistance()},
                {.Element = std::format("R-{}b", name),
                 .Quantity = LiveQuantity::RESISTANCE,
                 .Value = pot.GetSecondTrackResistance()}};
    }
    return {};
}

} // namespace Core
