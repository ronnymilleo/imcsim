/**
 * @file    transient_tab.cpp
 * @brief   The Transient tab of the Output window: the traces over time, logic lanes and their statistics.
 */

#include "windows/output_tabs/transient_tab.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "imgui_internal.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/statistics_table.h"
#include "results/trace_statistics.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/cursor_widgets.h"
#include "windows/output_tabs/logic_lane_plot.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/statistics_widgets.h"
#include "windows/output_tabs/step_widgets.h"
#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/// Width of the field for the size of the window, in font sizes.
constexpr float WINDOW_SIZE_WIDTH = 4.5f;
/// The label of that field: points of the plot, not the time steps of the run, which the status bar counts.
constexpr const char *WINDOW_SIZE_LABEL = "Plot points";

/**
 * @brief   The width of the labeled field for the size of the window, at the right end of the plot header.
 */
float GetWindowSizeWidth() {
    return ImGui::CalcTextSize(WINDOW_SIZE_LABEL).x + ImGui::GetStyle().ItemSpacing.x
         + ImGui::GetFontSize() * WINDOW_SIZE_WIDTH;
}

/// How many points a pixel of dragging adds to the size of the window.
constexpr float WINDOW_SIZE_DRAG_SPEED = 20.0f;
/// The instant of the transient shown on the schematic.
constexpr ImU32 PLAYHEAD_COLOR = IM_COL32(255, 96, 96, 255);

/**
 * @brief   The instant of the transient shown on the schematic, as a line along the plot.
 * @details Dragging it moves the instant and stops the playback, so the line stays where it was dropped.
 */
void DrawPlayhead(TransientPlayback &playback, const double from, const double to) {
    const ImVec4 color = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(PLAYHEAD_COLOR));
    double time = playback.Time;
    if (ImPlot::DragLineX(2, &time, color, 1.5f, ImPlotDragToolFlags_NoFit)) {
        playback.Time = std::clamp(time, from, to);
        playback.Playing = false;
    }
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose transient is shown; they must outlive the tab.
 * @param[in] controls   Runs of the analyses, to stop one from here while its plot grows; they must outlive the tab.
 * @param[in] playback   Instant of the transient shown on the schematic, drawn as a line that can be dragged, and the
 *                       window of points that plays; it must outlive the tab.
 */
TransientTab::TransientTab(Schematic &schematic,
                           ResultStore &results,
                           AnalysisControls &controls,
                           TransientPlayback &playback)
    : m_Schematic(schematic), m_Results(results), m_Controls(controls), m_Playback(playback) {
}

/**
 * @brief   Draws the tab: the step picker of a stepped run, the plots and the statistics.
 * @param[in] frame  The window of the transient and its math channels, worked out for this frame.
 */
void TransientTab::Draw(const FrameResults &frame) {
    if (!frame.TransientWindow) {
        ImGui::TextDisabled("No transient for the current circuit; pick it beside Run in the editor");
        return;
    }
    // One build of the circuit serves the plots and the statistics of the frame
    const LaneLayout layout = FindLaneLayout(m_Schematic, frame.TransientWindow->NodeVoltages.size(), m_ShowLogicLanes);
    DrawWithStatistics(
        [&] {
            if (const auto &steps = m_Results.GetTransientSteps()) {
                if (const std::optional<std::size_t> picked =
                        DrawStepSelector(steps->Parameter, steps->Unit, steps->Values, steps->Selected)) {
                    m_Results.SelectTransientStep(*picked);
                }
            }
            if (!frame.AnythingMeasured && frame.TransientMath.empty()) {
                ImGui::TextDisabled("%s", NOTHING_MEASURED);
            }
            DrawMathErrors(frame.TransientErrors);
            // The playback sits under the plot it moves the playhead of, sharing its width
            const Core::Transient &shown = *frame.TransientWindow;
            const bool has_playback = !shown.Times.empty();
            const float playback_height = has_playback ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
            if (ImGui::BeginChild("transient_plots", ImVec2(0.0f, -playback_height))) {
                DrawPlots(shown, frame.TransientMath, frame.StepWindows, layout);
            }
            ImGui::EndChild();
            if (has_playback) {
                DrawPlaybackBar(m_Playback, shown.Times.front(), shown.Times.back());
            }
        },
        [&] { DrawStatistics(*frame.TransientWindow, frame.TransientMath, layout); });
}

/**
 * @brief   The span the statistics measure, which the spectrum transforms too.
 * @return  The span between the cursors, or what the plot shows, or the whole run before the plot was first drawn,
 *          clipped to the run from the earlier end; an empty span without a transient.
 */
PlotSpan TransientTab::GetClippedMeasuredSpan() const {
    const auto &transient = m_Results.GetTransient();
    if (!transient || transient->Times.empty()) {
        return {};
    }
    return ClipSpanToRun(MeasuredSpan(m_Cursors, m_View),
                         {.From = transient->Times.front(), .To = transient->Times.back()});
}

/**
 * @brief   Tells whether the tab has something to export.
 * @param[in] frame  The window of the transient and its math channels.
 * @return  True with a transient and a measured trace or a math channel.
 */
bool TransientTab::CanExport(const FrameResults &frame) const {
    return frame.TransientWindow && (frame.AnythingMeasured || !frame.TransientMath.empty());
}

/**
 * @brief   The figure of the transient for the export.
 * @param[in] frame         The window of the transient and its math channels.
 * @param[in] is_whole_run  True for a CSV table of every point of the transient and its math channels, whatever the
 *                          window shows; false for the window as plotted.
 * @return  The figure, or an empty one without a transient.
 */
ExportFigure TransientTab::BuildFigure(const FrameResults &frame, const bool is_whole_run) const {
    if (const auto &transient = m_Results.GetTransient(); is_whole_run && transient) {
        static const std::vector<MathTrace> no_math;
        const LaneLayout layout = FindLaneLayout(m_Schematic, transient->NodeVoltages.size(), m_ShowLogicLanes);
        return BuildTransientFigure(*transient,
                                    frame.WholeRunMath != nullptr ? frame.WholeRunMath->Traces : no_math,
                                    layout.Signals,
                                    layout.Buses,
                                    m_Schematic,
                                    m_ShowBitValues,
                                    {.From = transient->Times.front(), .To = transient->Times.back()});
    }
    if (frame.TransientWindow) {
        const LaneLayout layout =
            FindLaneLayout(m_Schematic, frame.TransientWindow->NodeVoltages.size(), m_ShowLogicLanes);
        return BuildTransientFigure(*frame.TransientWindow,
                                    frame.TransientMath,
                                    layout.Signals,
                                    layout.Buses,
                                    m_Schematic,
                                    m_ShowBitValues,
                                    m_View);
    }
    return {};
}

/**
 * @brief   A transient that has not reached its stop time is either still running and can be stopped from here, since
 *          the Output window takes the front as its first chunk arrives, or was stopped and keeps the points it made.
 * @details A live run has no stop time to reach: stopped, it is paused, and goes on or is dropped from here.
 */
void TransientTab::DrawRunControls(const Core::Transient &transient) {
    if (m_Controls.IsLive()) {
        DrawLiveRunControls(transient);
        return;
    }
    const std::string reached = std::format("simulated up to {}s of {}s",
                                            Core::FormatValue(transient.Times.back()),
                                            Core::FormatValue(m_Schematic.GetSimulationSettings().Transient.StopTime));
    if (!m_Controls.IsRunning()) {
        ImGui::TextDisabled("%s", std::format("Stopped, {}", reached).c_str());
        return;
    }
    ImGui::BeginDisabled(m_Controls.IsStopping());
    if (ImGui::Button("Stop")) {
        m_Controls.StopRun();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", std::format("{}, {}", m_Controls.IsStopping() ? "Stopping" : "Running", reached).c_str());
}

/**
 * @brief   Stop pauses a live run, which then goes on with Continue or ends with Discard.
 */
void TransientTab::DrawLiveRunControls(const Core::Transient &transient) {
    const bool is_paused = m_Controls.IsLivePaused();
    ImGui::BeginDisabled(m_Controls.IsStopping());
    if (ImGui::Button(is_paused ? "Continue" : "Stop")) {
        if (is_paused) {
            m_Controls.ContinueLive();
        } else {
            m_Controls.StopRun();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        m_Controls.DiscardLive();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s",
                        std::format("{}, simulated up to {}s",
                                    is_paused ? "Live, paused" : "Live",
                                    Core::FormatValue(transient.Times.back()))
                            .c_str());
}

/**
 * @brief   A run longer than the plot shows is seen through a window of a size the user picks, which a scroll bar the
 *          width of the plot moves along the run, with a handle as large as the share of the run in view.
 * @details While the run goes on, the window can follow its newest points. The schematic plays the same window.
 */
void TransientTab::DrawWindowControls(const Core::Transient &shown) {
    if (shown.Times.empty()) {
        return;
    }
    const TransientWindow &window = m_Playback.Window;
    const WindowAvailability availability =
        GetWindowAvailability(window.GetTotalPoints(), window.GetSpan().has_value(), window.GetWindow().Count);
    const float width = ImGui::GetContentRegionAvail().x;
    DrawWindowHeaderRow(shown, availability.HasSize, width);
    if (availability.HasScrollbar) {
        DrawWindowScrollbar(width);
    }
}

/**
 * @brief   The row over the plot: Follow while the run goes on, the range shown, and at the right end the cursor toggle
 *          and the size of the window; in a narrow window those go one after the other on rows of their own rather
 *          than over the range.
 */
void TransientTab::DrawWindowHeaderRow(const Core::Transient &shown, const bool has_size, const float width) {
    const ImVec2 row_start = ImGui::GetCursorPos();
    if (m_Controls.IsRunning()) {
        TransientWindow &window = m_Playback.Window;
        bool follows = window.DoesFollow();
        if (ImGui::Checkbox("Follow", &follows)) {
            window.SetFollow(follows);
        }
        ImGui::SetItemTooltip("Keep the window on the newest points of the run");
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
    } else {
        ImGui::SetCursorPos(row_start);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Range");
    ImGui::SameLine();
    ImGui::TextUnformatted(
        std::format("{}s to {}s", Core::FormatValue(shown.Times.front()), Core::FormatValue(shown.Times.back()))
            .c_str());
    const float size_width = has_size ? GetWindowSizeWidth() + ImGui::GetFontSize() : 0.0f;
    ImGui::SameLine();
    const float right_edge = row_start.x + width;
    if (ImGui::GetCursorPosX() + ImGui::GetFontSize() + GetCursorToggleWidth() + size_width <= right_edge) {
        DrawCursorToggle(m_Cursors, right_edge - size_width);
        if (has_size) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(right_edge - GetWindowSizeWidth());
            DrawWindowSize();
        }
        return;
    }
    ImGui::NewLine();
    DrawCursorCheckbox(m_Cursors);
    if (has_size) {
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
        if (ImGui::GetCursorPosX() + GetWindowSizeWidth() > right_edge) {
            ImGui::NewLine();
        }
        DrawWindowSize();
    }
}

/**
 * @brief   The scroll bar that moves the window along the run, as wide as the plot.
 */
void TransientTab::DrawWindowScrollbar(const float width) {
    TransientWindow &window = m_Playback.Window;
    const ImRect bar(ImGui::GetCursorScreenPos(),
                     ImGui::GetCursorScreenPos() + ImVec2(width, ImGui::GetStyle().ScrollbarSize));
    ImGui::ItemSize(bar);
    ImS64 first = static_cast<ImS64>(window.GetWindow().First);
    if (ImGui::ScrollbarEx(bar,
                           ImGui::GetID("##window"),
                           ImGuiAxis_X,
                           &first,
                           static_cast<ImS64>(window.GetWindow().Count),
                           static_cast<ImS64>(window.GetTotalPoints()))) {
        window.SetFirst(static_cast<std::size_t>(first));
    }
    ImGui::SetItemTooltip("Drag to move the window along the run");
}

/**
 * @brief   The size of the window, labeled, at the cursor, which the header row places.
 */
void TransientTab::DrawWindowSize() {
    TransientWindow &window = m_Playback.Window;
    const float size_width = ImGui::GetFontSize() * WINDOW_SIZE_WIDTH;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", WINDOW_SIZE_LABEL);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(size_width);
    int points = window.GetPoints();
    if (ImGui::DragInt("##window_points",
                       &points,
                       WINDOW_SIZE_DRAG_SPEED,
                       MIN_WINDOW_POINTS,
                       MAX_WINDOW_POINTS,
                       "%d",
                       ImGuiSliderFlags_AlwaysClamp)) {
        window.SetPoints(points);
    }
    ImGui::SetItemTooltip("%s",
                          std::format("How many points the plot and the schematic show at a time, from {} to "
                                      "{}; drag, or double-click to type. The run has {} time steps",
                                      MIN_WINDOW_POINTS,
                                      MAX_WINDOW_POINTS,
                                      window.GetTotalPoints())
                              .c_str());
}

/**
 * @brief   The signals of logic parts are drawn as lanes in a plot under the analog traces, which share its time axis.
 * @details The analog plot is left out when it would be empty, so a circuit measured only at logic nodes shows the
 *          lanes alone.
 */
void TransientTab::DrawPlots(const Core::Transient &transient,
                             const std::vector<MathTrace> &math,
                             const std::optional<StepFamily<Core::Transient>> &steps,
                             const LaneLayout &layout) {
    if (!transient.IsComplete) {
        DrawRunControls(transient);
    }
    DrawWindowControls(transient);
    const std::size_t lane_count = layout.GetCount();
    std::vector<int> lane_nodes = ListNodes(layout.Signals);
    ShownTraces shown{.Nodes = ListMeasuredNodes(transient.NodeVoltages.size(), m_Schematic, lane_nodes),
                      .Currents = ListMeasuredCurrents(transient.Currents, m_Schematic),
                      .MathLabels = ListMathLabels(math)};
    const AnalogPlan plan = PlanAnalog(shown, math, std::move(lane_nodes), lane_count > 0);
    const TraceFits fits = TakeAnalogFits(transient, std::move(shown));
    // The fits go to the next plot, which is the plot of lanes when the analog one is left out
    SetNextFits(fits, math);
    const PlotHeights heights = GetPlotHeights(lane_count, plan.IsShown);
    if (plan.IsShown) {
        DrawAnalog(transient, math, steps, plan, heights.Analog);
    }
    if (lane_count > 0) {
        if (fits.IsNewResult) {
            ImPlot::SetNextAxisToFit(ImAxis_X1);
        }
        DrawLogicLanes(transient, layout, heights.Lanes);
    }
}

/**
 * @brief   Tells which axes of the analog plot fit this frame, and keeps what it drew for the next one.
 * @details Every axis fits for a new result, for a run that is still being made and has grown, and for a window that
 *          moved or changed size.
 * @param[in] transient  The window of the transient shown.
 * @param[in] shown      The traces the plot shows this frame.
 * @return  The axes that fit.
 */
TraceFits TransientTab::TakeAnalogFits(const Core::Transient &transient, ShownTraces shown) {
    // A transient that is still being made fits its axes as the chunks arrive, so the plot follows the run; once the
    // run ends, whether whole or stopped, the axes are the user's again
    const std::size_t data_version = m_Results.GetTransientDataVersion();
    const bool has_grown = std::exchange(m_DrawnDataVersion, data_version) != data_version && !transient.IsComplete;
    // The window that moved is the one this frame's points come from, so the fit sees the points it moved to
    const PointWindow window = m_Playback.Window.GetWindow();
    const PointWindow previous = std::exchange(m_DrawnWindow, window);
    const bool has_moved = previous.First != window.First || previous.Count != window.Count;
    return m_Fit.TakeFits(m_Results.GetTransientVersion(), std::move(shown), has_grown || has_moved);
}

/**
 * @brief   The analog plot: the other steps faint, the measured traces and the math channels, with the cursors and the
 *          playhead.
 */
void TransientTab::DrawAnalog(const Core::Transient &transient,
                              const std::vector<MathTrace> &math,
                              const std::optional<StepFamily<Core::Transient>> &steps,
                              const AnalogPlan &plan,
                              const float height) {
    if (!BeginTracePlot("##transient", ImVec2(-1.0f, height))) {
        return;
    }
    const std::string third_unit = ThirdAxisUnit(math);
    SetupAnalogAxes(math, plan, third_unit);
    const std::vector<PlottedTrace> plotted = PlotAnalogTraces(transient, math, steps, plan);
    DrawTimeMarkers(transient, plotted);
    KeepVisibleTime();
    ImPlot::EndPlot();
}

/**
 * @brief   Time, voltage, the current axis when a current is shown, and the axes of the math channels.
 * @param[in] math        The math channels of the plot.
 * @param[in] plan        What the plot shows.
 * @param[in] third_unit  Unit of the third axis, which must outlive the plot because ImPlot formats the ticks when it
 *                        ends.
 */
void TransientTab::SetupAnalogAxes(const std::vector<MathTrace> &math,
                                   const AnalogPlan &plan,
                                   const std::string &third_unit) {
    ImPlot::SetupAxes("Time",
                      "Voltage",
                      ImPlotAxisFlags_None,
                      VoltageAxisFlags(plan.HasVoltages, plan.ShowsCurrents || !math.empty()));
    // The plot of the lanes below shares the time axis
    if (plan.HasLanes) {
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    }
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (plan.ShowsCurrents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    SetupMathAxes(math, plan.ShowsCurrents, third_unit);
}

/**
 * @brief   Plots the other steps faint under the measured traces and the math channels, and labels the steps.
 * @return  The traces drawn, for the cursor readout.
 */
std::vector<PlottedTrace> TransientTab::PlotAnalogTraces(const Core::Transient &transient,
                                                         const std::vector<MathTrace> &math,
                                                         const std::optional<StepFamily<Core::Transient>> &steps,
                                                         const AnalogPlan &plan) {
    std::vector<StepTraces> step_traces;
    if (steps) {
        for (const Core::Transient &step : steps->Results) {
            step_traces.push_back({&step.Times, &step.NodeVoltages, &step.Currents});
        }
        PlotOtherSteps(step_traces, steps->Selected, m_Schematic, plan.ShowsCurrents, plan.LaneNodes);
    }
    std::vector<PlottedTrace> plotted;
    PlotNodes(transient.Times, transient.NodeVoltages, m_Schematic, "V", "", plotted, plan.LaneNodes);
    if (plan.ShowsCurrents) {
        PlotCurrents(transient.Times, transient.Currents, m_Schematic, "A", "", plotted);
    }
    PlotMath(transient.Times, math, "", plotted);
    if (steps) {
        LabelSteps(step_traces,
                   steps->Selected,
                   ListStepLabels(steps->Parameter, steps->Unit, steps->Values),
                   m_Schematic,
                   plan.ShowsCurrents,
                   plan.LaneNodes);
    }
    return plotted;
}

/**
 * @brief   The plot of logic lanes: the buses on top, the signals under them, the bit values when asked, and the
 * cursors and the playhead over them.
 */
void TransientTab::DrawLogicLanes(const Core::Transient &transient, const LaneLayout &layout, const float height) {
    if (!ImPlot::BeginPlot("##transient_logic",
                           ImVec2(-1.0f, height),
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        return;
    }
    const std::vector<std::string> names = ListLaneNames(layout, m_Schematic.GetConnectivity());
    SetupLaneAxes(names, m_View);
    DrawBusLanes(transient, layout);
    const std::vector<PlottedTrace> plotted = PlotSignalLanes(transient, layout, names);
    if (m_ShowBitValues) {
        DrawBitValueLabels(transient, layout);
    }
    DrawTimeMarkers(transient, plotted);
    KeepVisibleTime();
    ImPlot::EndPlot();
}

/**
 * @brief   The cursors, which the user drags, the playhead of the schematic and the readout of the traces under the
 *          pointer, over a plot against time.
 * @param[in] transient  The window of the transient shown.
 * @param[in] plotted    The traces drawn, for the readout.
 * @note    Call it inside a plot.
 */
void TransientTab::DrawTimeMarkers(const Core::Transient &transient, const std::vector<PlottedTrace> &plotted) {
    DrawCursors(m_Cursors, false);
    DrawPlayhead(m_Playback, transient.Times.front(), transient.Times.back());
    DrawCursorReadout(transient.Times, "s", false, plotted);
}

/**
 * @brief   Keeps the time range the plot shows, which the other plot and the statistics follow.
 * @note    Call it inside a plot.
 */
void TransientTab::KeepVisibleTime() {
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
}

/**
 * @brief   Like the measurements of an oscilloscope, for every shown trace; with the cursors, also the value of each
 *          trace at both and their difference, and the time between them with its frequency.
 */
void TransientTab::DrawStatistics(const Core::Transient &transient,
                                  const std::vector<MathTrace> &math,
                                  const LaneLayout &layout) {
    const PlotCursors &cursors = m_Cursors;
    const CursorReadings cursor_readings =
        ReadCursors(cursors, "s", {ReadCursorDifference(cursors, "s"), ReadCursorFrequency(cursors)});
    DrawStatisticsHeader(cursors.Shown ? "Between the cursors" : "Over the visible time");
    if (layout.HasLogicParts) {
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
        ImGui::Checkbox("Logic lanes", &m_ShowLogicLanes);
        ImGui::SetItemTooltip("Plot the signals of gates and flip-flops as lanes under the analog traces");
        if (m_ShowLogicLanes) {
            ImGui::SameLine();
            ImGui::Checkbox("Bit values", &m_ShowBitValues);
            ImGui::SetItemTooltip("Write 0, 1 or X in every cell of a lane, a cell being as wide as the shortest "
                                  "steady stretch, such as half a clock period");
        }
    }
    std::vector<PlottedTrace> traces = ListTraces(transient.NodeVoltages, transient.Currents, m_Schematic, "V", "A");
    for (const MathTrace &trace : math) {
        traces.push_back({trace.Label, &trace.Values, trace.Unit.c_str(), trace.Color});
    }
    if (traces.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    std::vector<MeasuredTrace> measured;
    for (const PlottedTrace &trace : traces) {
        measured.push_back({.Values = *trace.Values, .Unit = trace.Unit});
    }
    const std::vector<StatisticsRow> rows =
        BuildTransientStatistics(transient.Times, measured, MeasuredSpan(cursors, m_View), GetShownCursors(cursors));
    DrawTraceTable("trace_statistics", traces, rows, cursor_readings);
}

/**
 * @brief   Works out what the analog plot of a transient shows in a frame.
 * @param[in] shown       The traces it shows, logic lanes left out.
 * @param[in] math        The math channels of the plot.
 * @param[in] lane_nodes  Nodes drawn as logic lanes.
 * @param[in] has_lanes   Whether the plot of logic lanes is drawn, for signals or buses.
 * @return  The plan; the plot is left out when it would show nothing beside the lanes.
 */
AnalogPlan PlanAnalog(const ShownTraces &shown,
                      const std::vector<MathTrace> &math,
                      std::vector<int> lane_nodes,
                      const bool has_lanes) {
    const bool shows_currents = !shown.Currents.empty();
    const bool has_voltages = !shown.Nodes.empty() || UsesAxis(math, ImAxis_Y1);
    return {.LaneNodes = std::move(lane_nodes),
            .ShowsCurrents = shows_currents,
            .HasVoltages = has_voltages,
            .HasLanes = has_lanes,
            .IsShown = has_voltages || shows_currents || !math.empty() || !has_lanes};
}

/**
 * @brief   Tells what the window controls offer for a run of some points.
 * @param[in] total_points  Points of the whole run.
 * @param[in] has_span      Whether the window is a span of time, as a live run has, rather than a count of points.
 * @param[in] window_count  Points in the window now.
 * @return  No size or scroll bar for a run too short to cut into windows, or whose window holds it all; a live run has
 *          no size to pick, since it is seen a stop time at a time, and a scroll bar only while it is longer than that.
 */
WindowAvailability
GetWindowAvailability(const std::size_t total_points, const bool has_span, const std::size_t window_count) {
    const bool is_too_short =
        has_span ? window_count >= total_points : total_points <= static_cast<std::size_t>(MIN_WINDOW_POINTS);
    if (is_too_short) {
        return {.HasScrollbar = false, .HasSize = false};
    }
    return {.HasScrollbar = window_count < total_points, .HasSize = !has_span};
}

} // namespace GUI
