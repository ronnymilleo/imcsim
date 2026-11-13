/**
 * @file    logic_state.h
 * @brief   Reads the logic state of a voltage, as a gate or a logic analyzer does.
 */

#ifndef IMCSIM_LOGIC_STATE_H
#define IMCSIM_LOGIC_STATE_H

namespace GUI {

/**
 * @enum    LogicState
 * @brief   What a gate reads at a voltage: 0, 1, or unknown in between.
 */
enum class LogicState {
    LOW,
    HIGH,
    UNKNOWN
};

LogicState ReadLogicState(double voltage, double logic_level);
const char *GetLogicStateName(LogicState state);

} // namespace GUI

#endif // IMCSIM_LOGIC_STATE_H
