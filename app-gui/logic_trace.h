/**
 * @file    logic_trace.h
 * @brief   Reads logic levels from voltages, and lays logic signals out as lanes for the Output window.
 * @details Pure functions, so the plot only draws what they give.
 */

#ifndef IMCSIM_LOGIC_TRACE_H
#define IMCSIM_LOGIC_TRACE_H

#include "circuit.h"
#include <span>
#include <vector>

namespace GUI {

/**
 * @enum    LogicState
 * @brief   What a gate reads at a voltage: 0, 1, or unknown in between.
 */
enum class LogicState {
    Low,
    High,
    Unknown
};

/**
 * @struct  LogicNode
 * @brief   A node that a logic part is connected to, with the logic level its signal swings between.
 */
struct LogicNode {
    int Node = 0;
    double LogicLevel = 5.0;
};

/**
 * @struct  LogicSegment
 * @brief   A stretch of a signal that keeps one logic state.
 */
struct LogicSegment {
    double From = 0.0;
    double To = 0.0;
    LogicState State = LogicState::Low;
};

/**
 * @struct  LogicSignal
 * @brief   The voltages of one logic signal and the logic level it swings to.
 */
struct LogicSignal {
    std::span<const double> Voltages;
    double LogicLevel = 5.0;
};

// Height of one lane, and the offsets of the three states inside it; lanes are stacked from the bottom
inline constexpr double LaneStride = 1.5;
inline constexpr double LaneLowOffset = 0.0;
inline constexpr double LaneUnknownOffset = 0.5;
inline constexpr double LaneHighOffset = 1.0;

LogicState ReadLogicState(double voltage, double logic_level);
const char *GetLogicStateName(LogicState state);
std::vector<double> BuildLane(const std::vector<double> &voltages, double logic_level, std::size_t lane);
std::vector<LogicNode> FindLogicNodes(const Core::Circuit &circuit);
std::vector<LogicSegment> FindLogicSegments(std::span<const double> times, std::span<const double> voltages,
                                            double logic_level);
std::vector<std::vector<LogicSegment>> FindBitCells(std::span<const double> times, std::span<const LogicSignal> signals,
                                                    double from, double to);

} // namespace GUI

#endif // IMCSIM_LOGIC_TRACE_H
