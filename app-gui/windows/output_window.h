/**
 * @file    output_window.h
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "schematic.h"
#include "simulator.h"
#include "windows/app_window.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   OutputWindow
 * @brief   Plots the node voltages and component currents of the last transient over time, the last AC sweep as
 *          a Bode plot, and the last DC sweep against its swept source.
 * @details The results come from the Schematic, which the Simulation window fills, so the plots disappear as
 *          soon as the circuit changes. Plots show only the measured traces, picked with the Probe tool of the
 *          editor or in the list beside the plots; voltages take the colors the editor gives the nodes, and
 *          currents are dashed on the secondary Y axis on the right. Hovering a plot reads every shown trace at
 *          the nearest sample. The axes of each analysis fit its new results, and keep zoom and pan until the
 *          next one. The window floats instead of being docked, so the plots can be as large as needed.
 */
class OutputWindow : public AppWindow {
public:
    explicit OutputWindow(Schematic &schematic);
    ~OutputWindow() override = default;

private:
    Schematic &m_Schematic;
    // Result versions the axes were last fitted to
    std::optional<std::size_t> m_FittedTransient;
    std::optional<std::size_t> m_FittedACSweep;
    std::optional<std::size_t> m_FittedDCSweep;
    // Whether each analysis showed its current axis last frame; the axis is fitted when it appears again
    bool m_TransientShowedCurrents = false;
    bool m_ACSweepShowedCurrents = false;
    bool m_DCSweepShowedCurrents = false;

    void Draw() override;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);
    void DrawTransient(const Core::Transient &transient);
    void DrawACSweep(const Core::ACSweep &sweep);
    void DrawDCSweep(const Core::DCSweep &sweep);
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
