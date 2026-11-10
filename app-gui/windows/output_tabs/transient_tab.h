/**
 * @file    transient_tab.h
 * @brief   The Transient tab of the Output window: the traces over time, logic lanes and their statistics.
 */

#ifndef IMCSIM_TRANSIENT_TAB_H
#define IMCSIM_TRANSIENT_TAB_H

#include "analysis/analysis_controls.h"
#include "analysis/playback.h"
#include "analysis/point_window.h"
#include "document/schematic.h"
#include "plots/logic_bus.h"
#include "plots/logic_trace.h"
#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "windows/output_tabs/frame_results.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   TransientTab
 * @brief   Plots the window of the transient that plays, with the playhead of the schematic, two cursors and the
 *          statistics of every shown trace on the right, as an oscilloscope would.
 * @details The signals of logic parts get a lane each, in a plot of their own under the analog one that shares its
 *          time axis. A run in progress can be stopped from here, and a run longer than the window is moved through
 *          with a scroll bar. The axes fit a new result, a run that grows and a window that moves.
 */
class TransientTab {
public:
    TransientTab(Schematic &schematic, AnalysisControls &controls, TransientPlayback &playback);

    void Draw(const FrameResults &frame);
    PlotSpan GetMeasuredSpan() const;

    // Export
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildFigure(const FrameResults &frame, bool is_whole_run) const;

private:
    Schematic &m_Schematic;
    AnalysisControls &m_Controls;
    TransientPlayback &m_Playback;

    // Plot
    /// What the plot showed last frame, so its axes fit for a new result and when its traces change.
    TraceFitState m_Fit;
    PlotCursors m_Cursors;
    /// The visible time range, which the logic lanes share and the statistics measure.
    PlotSpan m_View;
    /// The data version of the transient drawn last frame, to fit the axes as a run that is still going on grows.
    std::size_t m_DrawnDataVersion = 0;
    /// The window the plot drew last frame, so a window that moved or changed size fits the axes.
    PointWindow m_DrawnWindow;

    // Logic lanes
    /// Whether the signals of logic parts get a lane each, in a plot of their own under the analog one.
    bool m_ShowLogicLanes = true;
    /// Whether each steady stretch of a lane is labeled 0, 1 or X.
    bool m_ShowBitValues = false;

    // Notes and window
    void DrawIncompleteNote(const Core::Transient &transient);
    void DrawLiveNote(const Core::Transient &transient);
    void DrawWindowControls(const Core::Transient &shown);
    void DrawWindowSize();

    // Plots
    void DrawPlots(const Core::Transient &transient, const std::vector<MathTrace> &math,
                   const std::optional<StepFamily<Core::Transient>> &steps);
    void DrawAnalog(const Core::Transient &transient, const std::vector<MathTrace> &math,
                    const std::optional<StepFamily<Core::Transient>> &steps, const std::vector<int> &lane_nodes,
                    bool show_currents, bool has_voltages, bool has_lanes, float height);
    void DrawLogicLanes(const Core::Transient &transient, const std::vector<BusLane> &buses,
                        const std::vector<LogicNode> &lanes, float height);
    std::vector<LogicNode> FindMeasuredLanes(std::size_t node_count) const;
    std::vector<BusLane> FindShownBuses(std::size_t node_count) const;

    // Statistics
    void DrawStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math);
};

} // namespace GUI

#endif // IMCSIM_TRANSIENT_TAB_H
