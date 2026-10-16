/**
 * @file    logic_trace.cpp
 * @brief   Reads logic levels from voltages, and lays logic signals out as lanes for the Output window.
 */

#include "logic_trace.h"

#include "components/logic_part.h"
#include <algorithm>
#include <map>

namespace GUI {

/**
 * @brief   Reads the logic state of a voltage, with the thresholds of the logic parts.
 * @param[in] voltage      Voltage of the node.
 * @param[in] logic_level  Logic level the signal swings to.
 * @return  High at or above 70% of the level, low at or below 30% of it, and unknown in between.
 */
LogicState ReadLogicState(const double voltage, const double logic_level) {
    if (voltage >= logic_level * Core::LogicHighFraction) {
        return LogicState::High;
    }
    if (voltage <= logic_level * Core::LogicLowFraction) {
        return LogicState::Low;
    }
    return LogicState::Unknown;
}

/**
 * @brief   Names a logic state as a logic analyzer shows it.
 * @param[in] state  A logic state.
 * @return  "0", "1" or "X".
 */
const char *GetLogicStateName(const LogicState state) {
    switch (state) {
    case LogicState::Low:
        return "0";
    case LogicState::High:
        return "1";
    case LogicState::Unknown:
        break;
    }
    return "X";
}

/**
 * @brief   Turns the voltages of a node into the height of its trace in a stack of lanes.
 * @param[in] voltages     Voltage at every sample.
 * @param[in] logic_level  Logic level the signal swings to.
 * @param[in] lane         Position of the lane from the bottom of the stack, 0 for the lowest.
 * @return  One height per sample: the bottom of the lane while low, a unit above it while high, and halfway
 *          between while unknown.
 */
std::vector<double> BuildLane(const std::vector<double> &voltages, const double logic_level, const std::size_t lane) {
    const double bottom = static_cast<double>(lane) * LaneStride;
    std::vector<double> heights;
    heights.reserve(voltages.size());
    for (const double voltage : voltages) {
        switch (ReadLogicState(voltage, logic_level)) {
        case LogicState::Low:
            heights.push_back(bottom + LaneLowOffset);
            break;
        case LogicState::High:
            heights.push_back(bottom + LaneHighOffset);
            break;
        case LogicState::Unknown:
            heights.push_back(bottom + LaneUnknownOffset);
            break;
        }
    }
    return heights;
}

/**
 * @brief   Finds the nodes that gates and flip-flops are connected to, which are logic signals.
 * @param[in] circuit  Circuit of a simulation.
 * @return  One entry per node, in node order and without ground, with the largest logic level among the logic parts
 *          on it.
 */
std::vector<LogicNode> FindLogicNodes(const Core::Circuit &circuit) {
    std::map<int, double> levels;
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        if (!Core::IsLogicPart(entry.Part->GetType())) {
            continue;
        }
        const double level = static_cast<const Core::LogicPart &>(*entry.Part).GetParameters().LogicLevel;
        for (const int node : entry.Nodes) {
            if (node != 0) {
                levels[node] = std::max(levels[node], level);
            }
        }
    }
    std::vector<LogicNode> nodes;
    for (const auto &[node, level] : levels) {
        nodes.push_back({.Node = node, .LogicLevel = level});
    }
    return nodes;
}

/**
 * @brief   Splits a signal into the stretches that keep one logic state, as a logic analyzer labels its lanes.
 * @param[in] times        Time of every sample, ascending.
 * @param[in] voltages     Voltage at every sample; samples beyond the shorter of the two are ignored.
 * @param[in] logic_level  Logic level the signal swings to.
 * @return  Segments in time order that follow one another: each starts at the first sample of its state and ends where
 *          the next begins, and the last ends at the last sample. Empty for no samples.
 */
std::vector<LogicSegment> FindLogicSegments(const std::span<const double> times, const std::span<const double> voltages,
                                            const double logic_level) {
    std::vector<LogicSegment> segments;
    const std::size_t count = std::min(times.size(), voltages.size());
    for (std::size_t index = 0; index < count; ++index) {
        const LogicState state = ReadLogicState(voltages[index], logic_level);
        if (!segments.empty() && segments.back().State == state) {
            segments.back().To = times[index];
            continue;
        }
        if (!segments.empty()) {
            segments.back().To = times[index];
        }
        segments.push_back({.From = times[index], .To = times[index], .State = state});
    }
    return segments;
}

} // namespace GUI
