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
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  PlotCursors
 * @brief   The two vertical cursors of a plot, A and B, as on an oscilloscope.
 * @details They are placed in the visible range when turned on, and the user drags them from there.
 */
struct PlotCursors {
    bool Shown = false;
    bool Placed = false;
    double A = 0.0;
    double B = 0.0;
};

/**
 * @struct  PlotSpan
 * @brief   A range of the X axis of a plot, such as what it shows.
 */
struct PlotSpan {
    double From = 0.0;
    double To = 0.0;
};

/**
 * @class   OutputWindow
 * @brief   Plots the node voltages and component currents of the last transient over time, the last AC sweep as
 *          a Bode plot, and the last DC sweep against its swept source.
 * @details The results come from the Schematic, which the Simulation window fills, so the plots disappear as
 *          soon as the circuit changes. Plots show only the measured traces, picked with the Probe tool of the
 *          editor or in the list beside the plots; voltages take the colors the editor gives the nodes, and
 *          currents are dashed on the secondary Y axis on the right. Hovering a plot reads every shown trace at
 *          the nearest sample. The axes of each analysis fit its new results, and keep zoom and pan until the
 *          next one; the voltage and current axes also fit whenever their measured traces change. Like an
 *          oscilloscope, the transient and the Bode plots have two cursors and a panel on their right that
 *          measures every shown trace: the transient between the cursors or over what is visible, the Bode plot
 *          over the whole sweep. The window floats instead of being docked, so the plots can be as large as
 *          needed.
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
    // Nodes and currents each analysis plotted last frame; the axis of each fits whenever they change
    std::vector<int> m_TransientShownNodes;
    std::vector<int> m_ACSweepShownNodes;
    std::vector<int> m_DCSweepShownNodes;
    std::vector<std::string> m_TransientShownCurrents;
    std::vector<std::string> m_ACSweepShownCurrents;
    std::vector<std::string> m_DCSweepShownCurrents;
    // Cursors and visible X range of the transient and the Bode plots; both Bode plots share theirs
    PlotCursors m_TransientCursors;
    PlotCursors m_ACSweepCursors;
    PlotSpan m_TransientView;
    PlotSpan m_ACSweepView{.From = 1.0, .To = 1e6};

    void Draw() override;
    void DrawTraceList(std::size_t node_count, const std::vector<Core::ComponentTrace> &currents);
    void DrawTransient(const Core::Transient &transient);
    void DrawACSweep(const Core::ACSweep &sweep);
    void DrawDCSweep(const Core::DCSweep &sweep);

    // Statistics
    void DrawTransientStatistics(const Core::Transient &transient);
    void DrawACSweepStatistics(const Core::ACSweep &sweep);
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
