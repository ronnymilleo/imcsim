/**
 * @file    measurements.h
 * @brief   The traces the plots show: measured voltages and currents, math channels and logic buses.
 */

#ifndef IMCSIM_MEASUREMENTS_H
#define IMCSIM_MEASUREMENTS_H

#include "schematic/connectivity.h"
#include "schematic/logic_bus_definition.h"
#include "schematic/math_channel_definition.h"
#include "schematic/schematic_file.h"
#include "schematic/voltage_probes.h"
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   Measurements
 * @brief   What the plots show of a schematic, kept by key (node number or current name) across edits.
 * @details Measurements are view state shared by the editor and the plots: they are saved with the file, so it opens
 *          ready to plot, but they stay out of the history and changing them does not count as an unsaved change.
 *          Voltages are probes kept by the points of their node, since small edits rarely renumber nodes and the
 *          probes follow when they do; the operations that need the nodes take the schematic's connectivity.
 */
class Measurements {
public:
    // Voltages
    bool IsVoltageMeasured(int node) const;
    void SetVoltageMeasured(int node, bool measured, const Connectivity &connectivity);
    void FollowNodes(const Connectivity &connectivity);
    void ClearVoltageProbes();

    // Currents
    bool IsCurrentMeasured(const std::string &name) const;
    void SetCurrentMeasured(const std::string &name, bool measured);
    void RenamePart(const std::string &old_name, const std::string &new_name);

    // Math channels
    const std::vector<MathChannel> &GetMathChannels() const;
    void SetMathChannels(std::vector<MathChannel> channels);

    // Logic buses
    const std::vector<LogicBus> &GetBuses() const;
    void SetBuses(std::vector<LogicBus> buses);

    // Files
    void Clear();
    SavedMeasurements Save(const Connectivity &connectivity) const;
    void Restore(const SavedMeasurements &measurements, const Connectivity &connectivity);

private:
    /// Probes on nodes.
    std::vector<VoltageProbe> m_VoltageProbes;
    /// Current names such as "R1" or "Q1.C".
    std::set<std::string> m_MeasuredCurrents;
    std::vector<MathChannel> m_MathChannels;
    /// Logic signals the transient plot reads together as numbers.
    std::vector<LogicBus> m_Buses;
};

} // namespace GUI

#endif // IMCSIM_MEASUREMENTS_H
