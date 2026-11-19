/**
 * @file    measurements.cpp
 * @brief   The traces the plots show: measured voltages and currents, math channels and logic buses.
 */

#include "schematic/measurements.h"

#include "schematic/part_names.h"
#include <algorithm>
#include <optional>
#include <utility>

namespace GUI {

/**
 * @brief   Tells whether the plots show the voltage of a node.
 * @param[in] node  Node number, as numbered by the connectivity the probes last followed.
 * @return  True when the node is measured.
 */
bool Measurements::IsVoltageMeasured(const int node) const {
    return std::ranges::any_of(m_VoltageProbes, [node](const VoltageProbe &probe) { return probe.Node == node; });
}

/**
 * @brief   Adds or removes the voltage of a node from the plots.
 * @param[in] node          Node number, as numbered by connectivity.
 * @param[in] measured      Whether to show it.
 * @param[in] connectivity  The nodes of the schematic, which give the points a new probe keeps.
 */
void Measurements::SetVoltageMeasured(const int node, const bool measured, const Connectivity &connectivity) {
    if (!measured) {
        std::erase_if(m_VoltageProbes, [node](const VoltageProbe &probe) { return probe.Node == node; });
    } else if (!IsVoltageMeasured(node)) {
        m_VoltageProbes.push_back({.Points = connectivity.GetPointsOfNode(node), .Node = node});
    }
}

/**
 * @brief   Moves every voltage probe to the number its node has in nodes just rebuilt.
 * @param[in] connectivity  The nodes as they are now.
 */
void Measurements::FollowNodes(const Connectivity &connectivity) {
    m_VoltageProbes = FollowVoltageProbes(std::move(m_VoltageProbes), connectivity);
}

/**
 * @brief   Removes every voltage probe and keeps the rest, as when the content they were on is replaced.
 */
void Measurements::ClearVoltageProbes() {
    m_VoltageProbes.clear();
}

/**
 * @brief   Tells whether the plots show a current.
 * @param[in] name  Current name as in the results, such as "R1" or "Q1.C".
 * @return  True when the current is measured.
 */
bool Measurements::IsCurrentMeasured(const std::string &name) const {
    return m_MeasuredCurrents.contains(name);
}

/**
 * @brief   Adds or removes a current from the plots.
 * @param[in] name      Current name as in the results, such as "R1" or "Q1.C".
 * @param[in] measured  Whether to show it.
 */
void Measurements::SetCurrentMeasured(const std::string &name, const bool measured) {
    if (measured) {
        m_MeasuredCurrents.insert(name);
    } else {
        m_MeasuredCurrents.erase(name);
    }
}

/**
 * @brief   Keeps the measured currents of a renamed part, and the math channels that read them, on that part.
 * @param[in] old_name  The name the part had.
 * @param[in] new_name  The name it has now.
 */
void Measurements::RenamePart(const std::string &old_name, const std::string &new_name) {
    std::set<std::string> measured;
    for (const std::string &current : m_MeasuredCurrents) {
        measured.insert(RenameCurrent(current, old_name, new_name));
    }
    m_MeasuredCurrents = std::move(measured);
    for (MathChannel &channel : m_MathChannels) {
        channel.First = RenameCurrentsIn(channel.First, old_name, new_name);
        channel.Second = RenameCurrentsIn(channel.Second, old_name, new_name);
        channel.Expression = RenameCurrentsIn(channel.Expression, old_name, new_name);
    }
}

/**
 * @brief   Returns the math channels the plots add to the measured traces.
 * @return  The channels, in the order they were added.
 */
const std::vector<MathChannel> &Measurements::GetMathChannels() const {
    return m_MathChannels;
}

/**
 * @brief   Replaces the math channels, as the Output window does when the user adds, edits or removes one.
 * @param[in] channels  New channels.
 */
void Measurements::SetMathChannels(std::vector<MathChannel> channels) {
    m_MathChannels = std::move(channels);
}

/**
 * @brief   Returns the logic buses the transient plot shows as lanes of numbers.
 * @return  The buses, in the order they were added.
 */
const std::vector<LogicBus> &Measurements::GetBuses() const {
    return m_Buses;
}

/**
 * @brief   Replaces the logic buses, as the Output window does when the user adds, edits or removes one.
 * @param[in] buses  New buses.
 */
void Measurements::SetBuses(std::vector<LogicBus> buses) {
    m_Buses = std::move(buses);
}

/**
 * @brief   Removes every measurement, so the plots start empty; for a new or opened schematic.
 */
void Measurements::Clear() {
    m_VoltageProbes.clear();
    m_MeasuredCurrents.clear();
    m_MathChannels.clear();
    m_Buses.clear();
}

/**
 * @brief   Describes the measurements for a file, with each voltage as a point of its node.
 * @param[in] connectivity  The nodes of the schematic, which the probes have followed.
 * @return  The measured voltages and currents, the math channels and the buses; nodes that no longer exist are left
 *          out.
 */
SavedMeasurements Measurements::Save(const Connectivity &connectivity) const {
    SavedMeasurements measurements;
    for (const VoltageProbe &probe : m_VoltageProbes) {
        if (probe.Node) {
            if (const std::optional<GridPoint> point = connectivity.FindPointOfNode(*probe.Node)) {
                measurements.Voltages.push_back(*point);
            }
        }
    }
    measurements.Currents.assign(m_MeasuredCurrents.begin(), m_MeasuredCurrents.end());
    measurements.MathChannels = m_MathChannels;
    measurements.Buses = m_Buses;
    return measurements;
}

/**
 * @brief   Replaces the measurements with those read from a file.
 * @param[in] measurements  Measured voltages and currents, math channels and buses.
 * @param[in] connectivity  The nodes of the content the measurements refer to.
 * @note    Points that connect to nothing, or to ground, are skipped. Currents of parts that do not exist are kept
 *          but plot nothing, like after deleting a measured part.
 */
void Measurements::Restore(const SavedMeasurements &measurements, const Connectivity &connectivity) {
    Clear();
    for (const GridPoint point : measurements.Voltages) {
        if (const std::optional<int> node = connectivity.GetNode(point); node && *node != 0) {
            SetVoltageMeasured(*node, true, connectivity);
        }
    }
    m_MeasuredCurrents.insert(measurements.Currents.begin(), measurements.Currents.end());
    m_MathChannels = measurements.MathChannels;
    m_Buses = measurements.Buses;
}

} // namespace GUI
