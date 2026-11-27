/**
 * @file    transient_tab.h
 * @brief   The Transient tab of the Output window: the traces over time, logic lanes and their statistics.
 */

#ifndef IMCSIM_TRANSIENT_TAB_H
#define IMCSIM_TRANSIENT_TAB_H

#include "analysis/analysis_controls.h"
#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "results/logic_bus.h"
#include "results/logic_trace.h"
#include "results/playback.h"
#include "results/point_window.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "windows/output_tabs/frame_results.h"
#include "windows/output_tabs/logic_lane_plot.h"
#include "windows/output_tabs/plot_widgets.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  WindowAvailability
 * @brief   What the controls of the window of points offer for the run shown.
 */
struct WindowAvailability {
    /// Whether a scroll bar moves the window along the run, which is longer than the window.
    bool HasScrollbar = false;
    bool HasSize = false; ///< Whether the user picks how many points the window holds.

    /**
     * @brief   Checks whether two WindowAvailability instances hold equal values.
     * @return  True when every field is equal.
     */
    bool operator==(const WindowAvailability &) const = default;
};

/**
 * @struct  AnalogPlan
 * @brief   What the analog plot of a transient shows in a frame, worked out once from the traces and the lanes.
 */
struct AnalogPlan {
    std::vector<int> LaneNodes; ///< Nodes drawn as logic lanes, which the analog plot leaves out.
    bool ShowsCurrents = false;
    /// Whether a voltage is shown, measured or as a math channel, so the voltage axis keeps its label and ticks.
    bool HasVoltages = false;
    bool HasLanes = false; ///< Whether the plot of logic lanes under it shares its time axis.
    /// Left out when it would be empty, so a circuit measured only at logic nodes shows the lanes alone.
    bool IsShown = false;

    /**
     * @brief   Checks whether two AnalogPlan instances hold equal values.
     * @return  True when every field is equal.
     */
    bool operator==(const AnalogPlan &) const = default;
};

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
    TransientTab(Schematic &schematic, ResultStore &results, AnalysisControls &controls, TransientPlayback &playback);

    void Draw(const FrameResults &frame);
    PlotSpan GetClippedMeasuredSpan() const;

    // Export
    bool CanExport(const FrameResults &frame) const;
    ExportFigure BuildFigure(const FrameResults &frame, bool is_whole_run) const;

private:
    Schematic &m_Schematic;
    ResultStore &m_Results;
    AnalysisControls &m_Controls;
    TransientPlayback &m_Playback;

    // Plot
    /// What the plot showed last frame, so its axes fit for a new result and when its traces change.
    TraceFitState m_Fit;
    PlotCursors m_Cursors;
    PlotSpan m_View; ///< The visible time range, which the logic lanes share and the statistics measure.
    /// The data version of the transient drawn last frame, to fit the axes as a run that is still going on grows.
    std::size_t m_DrawnDataVersion = 0;
    /// The window the plot drew last frame, so a window that moved or changed size fits the axes.
    PointWindow m_DrawnWindow;

    // Logic lanes
    /// Whether the signals of logic parts get a lane each, in a plot of their own under the analog one.
    bool m_ShowLogicLanes = true;
    bool m_ShowBitValues = false; ///< Whether each steady stretch of a lane is labeled 0, 1 or X.

    // Notes and window
    void DrawRunControls(const Core::Transient &transient);
    void DrawLiveRunControls(const Core::Transient &transient);
    void DrawWindowControls(const Core::Transient &shown);
    void DrawWindowHeaderRow(const Core::Transient &shown, bool has_size, float width);
    void DrawWindowScrollbar(float width);
    void DrawWindowSize();

    // Plots
    void DrawPlots(const Core::Transient &transient,
                   const std::vector<MathTrace> &math,
                   const std::optional<StepFamily<Core::Transient>> &steps,
                   const LaneLayout &layout);
    TraceFits TakeAnalogFits(const Core::Transient &transient, ShownTraces shown);
    void DrawAnalog(const Core::Transient &transient,
                    const std::vector<MathTrace> &math,
                    const std::optional<StepFamily<Core::Transient>> &steps,
                    const AnalogPlan &plan,
                    float height);
    void SetupAnalogAxes(const std::vector<MathTrace> &math, const AnalogPlan &plan, const std::string &third_unit);
    std::vector<PlottedTrace> PlotAnalogTraces(const Core::Transient &transient,
                                               const std::vector<MathTrace> &math,
                                               const std::optional<StepFamily<Core::Transient>> &steps,
                                               const AnalogPlan &plan);
    void DrawLogicLanes(const Core::Transient &transient, const LaneLayout &layout, float height);
    void DrawTimeMarkers(const Core::Transient &transient, const std::vector<PlottedTrace> &plotted);
    void KeepVisibleTime();

    // Statistics
    void DrawStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math, const LaneLayout &layout);
};

// Window
WindowAvailability GetWindowAvailability(std::size_t total_points, bool has_span, std::size_t window_count);

// Analog plot
AnalogPlan
PlanAnalog(const ShownTraces &shown, const std::vector<MathTrace> &math, std::vector<int> lane_nodes, bool has_lanes);

} // namespace GUI

#endif // IMCSIM_TRANSIENT_TAB_H
