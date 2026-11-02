/**
 * @file    logic_trace.cpp
 * @brief   Reads logic levels from voltages, and lays logic signals out as lanes for the Output window.
 */

#include "logic_trace.h"

#include "components/logic_part.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

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

/**
 * @brief   Turns the voltages of a node into the height of its trace in a stack of lanes.
 * @param[in] voltages     Voltage at every sample.
 * @param[in] logic_level  Logic level the signal swings to.
 * @param[in] lane         Position of the lane from the bottom of the stack, 0 for the lowest.
 * @return  One height per sample: the bottom of the lane while low, a unit above it while high, and halfway
 *          between while unknown.
 */
std::vector<double> BuildLane(const std::vector<double> &voltages, const double logic_level, const std::size_t lane) {
    const double bottom = static_cast<double>(lane) * LANE_STRIDE;
    std::vector<double> heights;
    heights.reserve(voltages.size());
    for (const double voltage : voltages) {
        switch (ReadLogicState(voltage, logic_level)) {
        case LogicState::LOW:
            heights.push_back(bottom + LANE_LOW_OFFSET);
            break;
        case LogicState::HIGH:
            heights.push_back(bottom + LANE_HIGH_OFFSET);
            break;
        case LogicState::UNKNOWN:
            heights.push_back(bottom + LANE_UNKNOWN_OFFSET);
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

namespace {

// A stretch shorter than this share of the typical one is a glitch, which does not set the width of a cell
constexpr double GLITCH_SHARE = 0.2;
// Gaps this close to the shortest are the same width
constexpr double SAME_WIDTH = 1.05;
// Beyond this many cells in view they would be too narrow to label, so none are listed
constexpr double MAX_CELLS = 5000.0;

// What the signal reads at the sample nearest a time
LogicState ReadStateAt(const std::span<const double> times, const std::span<const double> voltages,
                       const double logic_level, const double time) {
    const auto after = std::ranges::lower_bound(times, time);
    auto index = static_cast<std::size_t>(after - times.begin());
    if (index >= times.size()) {
        index = times.size() - 1;
    } else if (index > 0 && time - times[index - 1] < times[index] - time) {
        --index;
    }
    return ReadLogicState(voltages[std::min(index, voltages.size() - 1)], logic_level);
}

// Where the cells start and how wide they are
struct CellGrid {
    double Start = 0.0;
    double Width = 1.0;
};

// The stretches that keep a defined state, joined when the signal leaves a state and comes back to it through the
// unknown band, which is a glitch or a slow edge and not a change of the data
std::vector<LogicSegment> JoinDefiniteSegments(const std::vector<LogicSegment> &segments) {
    std::vector<LogicSegment> joined;
    for (const LogicSegment &segment : segments) {
        if (segment.State == LogicState::UNKNOWN) {
            continue;
        }
        if (!joined.empty() && joined.back().State == segment.State) {
            joined.back().To = segment.To;
        } else {
            joined.push_back(segment);
        }
    }
    return joined;
}

// The changes of every signal, with those that follow another closely merged into the first, since a glitch is
// not a change of the data. The cells are as wide as the shortest gap between changes, taken as the mean of those
// within a few percent of it so the quantizing of the sample times does not add up over many cells, and start at the
// first change, so the edges of a clock are the edges of the cells
CellGrid FindCellGrid(const std::span<const double> times, const std::span<const LogicSignal> signals) {
    std::vector<double> changes;
    std::vector<double> durations;
    for (const LogicSignal &signal : signals) {
        const std::vector<LogicSegment> segments =
            JoinDefiniteSegments(FindLogicSegments(times, signal.Voltages, signal.LogicLevel));
        for (std::size_t index = 1; index < segments.size(); ++index) {
            changes.push_back(segments[index].From);
        }
        for (std::size_t index = 1; index + 1 < segments.size(); ++index) {
            durations.push_back(segments[index].To - segments[index].From);
        }
    }
    const CellGrid whole{.Start = times.front(), .Width = times.back() - times.front()};
    if (changes.empty()) {
        return whole;
    }
    std::ranges::sort(changes);
    std::ranges::sort(durations);
    const double typical = durations.empty() ? whole.Width : durations[durations.size() / 2];
    std::vector<double> merged = {changes.front()};
    for (const double change : changes) {
        if (change - merged.back() > typical * GLITCH_SHARE) {
            merged.push_back(change);
        }
    }
    if (merged.size() < 2) {
        return {.Start = merged.front(), .Width = whole.Width};
    }
    std::vector<double> gaps;
    for (std::size_t index = 1; index < merged.size(); ++index) {
        gaps.push_back(merged[index] - merged[index - 1]);
    }
    const double shortest = *std::ranges::min_element(gaps);
    double total = 0.0;
    int count = 0;
    for (const double gap : gaps) {
        if (gap <= shortest * SAME_WIDTH) {
            total += gap;
            ++count;
        }
    }
    return {.Start = merged.front(), .Width = total / count};
}

} // namespace

/**
 * @brief   Splits logic signals into cells of one width, like the bit cells of a logic analyzer, and reads each one.
 * @param[in] times    Time of every sample, ascending.
 * @param[in] signals  The signals, each sampled at `times`.
 * @param[in] from     Start of the time to list cells for, such as what a plot shows.
 * @param[in] to       End of that time.
 * @return  For every signal, the cells that overlap [from, to] in time order, each clipped to the samples and reading
 *          the state at its middle, so a glitch inside a cell does not show. A cell is as wide as the shortest gap
 *          between changes of any signal, which is half the period of a clock, ignoring glitches that follow a change
 *          closely, and the cells start at the first change, so the edges of a clock are the edges of the cells.
 *          Computed over every sample, so the cells do not move as the view does. Signals that never change are one
 *          cell as wide as the samples. Empty lists when there are fewer than two samples, or when more than 5000
 *          cells would be in view.
 */
std::vector<std::vector<LogicSegment>> FindBitCells(const std::span<const double> times,
                                                    const std::span<const LogicSignal> signals, const double from,
                                                    const double to) {
    std::vector<std::vector<LogicSegment>> cells(signals.size());
    if (times.size() < 2) {
        return cells;
    }
    const double first_time = times.front();
    const double last_time = times.back();
    const CellGrid grid = FindCellGrid(times, signals);
    const double start = grid.Start;
    const double width = grid.Width;
    const double visible_from = std::max(from, first_time);
    const double visible_to = std::min(to, last_time);
    if (!(width > 0.0) || !(visible_to > visible_from) || (visible_to - visible_from) / width > MAX_CELLS) {
        return cells;
    }
    const double first_cell = std::floor((visible_from - start) / width);
    bool joins_next = false;
    for (double cell = first_cell;; cell += 1.0) {
        double cell_from = std::max(start + cell * width, first_time);
        const double cell_to = std::min(start + (cell + 1.0) * width, last_time);
        // What is left of a cell at either end of the samples is too narrow to read, so it joins its neighbor
        if (cell_to - cell_from < width * GLITCH_SHARE) {
            if (cell_to >= last_time) {
                for (std::vector<LogicSegment> &lane : cells) {
                    if (!lane.empty()) {
                        lane.back().To = last_time;
                    }
                }
                break;
            }
            joins_next = true;
            continue;
        }
        if (std::exchange(joins_next, false)) {
            cell_from = first_time;
        }
        if (cell_from >= visible_to) {
            break;
        }
        if (cell_to <= visible_from) {
            continue;
        }
        for (std::size_t signal = 0; signal < signals.size(); ++signal) {
            const LogicState state =
                ReadStateAt(times, signals[signal].Voltages, signals[signal].LogicLevel, (cell_from + cell_to) / 2.0);
            cells[signal].push_back({.From = cell_from, .To = cell_to, .State = state});
        }
    }
    return cells;
}

} // namespace GUI
