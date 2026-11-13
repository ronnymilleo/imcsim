/**
 * @file    logic_state.cpp
 * @brief   Reads the logic state of a voltage, as a gate or a logic analyzer does.
 */

#include "base/logic_state.h"

#include "circuit/components/logic_part.h"

namespace GUI {

/**
 * @brief   Reads the logic state of a voltage, with the thresholds of the logic parts.
 * @param[in] voltage      Voltage of the node.
 * @param[in] logic_level  Logic level the signal swings to.
 * @return  High at or above 70% of the level, low at or below 30% of it, and unknown in between.
 */
LogicState ReadLogicState(const double voltage, const double logic_level) {
    if (voltage >= logic_level * Core::LOGIC_HIGH_FRACTION) {
        return LogicState::HIGH;
    }
    if (voltage <= logic_level * Core::LOGIC_LOW_FRACTION) {
        return LogicState::LOW;
    }
    return LogicState::UNKNOWN;
}

/**
 * @brief   Names a logic state as a logic analyzer shows it.
 * @param[in] state  A logic state.
 * @return  "0", "1" or "X".
 */
const char *GetLogicStateName(const LogicState state) {
    switch (state) {
    case LogicState::LOW:
        return "0";
    case LogicState::HIGH:
        return "1";
    case LogicState::UNKNOWN:
        break;
    }
    return "X";
}

} // namespace GUI
