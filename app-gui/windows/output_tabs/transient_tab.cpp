/**
 * @file    transient_tab.cpp
 * @brief   The Transient tab of the Output window: the traces over time, logic lanes and their statistics.
 */

#include "windows/output_tabs/transient_tab.h"

#include "drawing/node_colors.h"
#include "drawing/theme.h"
#include "editing/probing.h"
#include "imgui_internal.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "plots/statistics_table.h"
#include "plots/trace_statistics.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/plot_widgets.h"
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

/**
 * @brief   The width of the labeled field for the size of the window, at the right end of the plot header.
 */
float GetWindowSizeWidth() {
    return ImGui::CalcTextSize("Points").x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetFontSize() * WINDOW_SIZE_WIDTH;
}
/// How many points a pixel of dragging adds to the size of the window.
constexpr float WINDOW_SIZE_DRAG_SPEED = 20.0f;
/// The instant of the transient shown on the schematic.
constexpr ImU32 PLAYHEAD_COLOR = IM_COL32(255, 96, 96, 255);
/// Height of the plot of logic lanes for its axis, in font sizes.
constexpr float LANE_PLOT_MARGIN = 3.5f;
/// Height of each lane of the plot, in font sizes.
constexpr float LANE_ROW_HEIGHT = 2.2f;
/// Most of the room the plot of logic lanes may take.
constexpr float MAX_LANE_SHARE = 0.6f;
/// Room around the digit of a bit label, in font sizes.
constexpr float BIT_BOX_PADDING = 0.25f;
/// Corner rounding of the box of a bit label.
constexpr float BIT_BOX_ROUNDING = 3.0f;
/// How far the ends of a value of a bus slant in, in font sizes, as a logic analyzer draws a change of a bus.
constexpr float BUS_SLANT = 0.3f;
/// How much of the lane color fills a value of a bus that is unknown.
constexpr float UNKNOWN_BUS_FILL_ALPHA = 0.25f;

/**
 * @brief   The measured nodes of a result that logic parts are connected to, which the transient plots as lanes.
 */
std::vector<LogicNode> ListMeasuredLogicNodes(const std::vector<LogicNode> &logic_nodes, const std::size_t node_count,
                                              const Schematic &schematic) {
    std::vector<LogicNode> measured;
    for (const LogicNode &logic_node : logic_nodes) {
        if (static_cast<std::size_t>(logic_node.Node) < node_count && schematic.IsVoltageMeasured(logic_node.Node)) {
            measured.push_back(logic_node);
        }
    }
    return measured;
}

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

/**
 * @brief   Writes 0, 1 or X in a box at the middle of every cell of a lane that has room for it, within what the plot
 *          shows, so zooming out drops the labels that no longer fit.
 */
void DrawBitValues(const std::vector<LogicSegment> &cells, const std::size_t lane_position, const ImU32 color) {
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const double middle = static_cast<double>(lane_position) * LANE_STRIDE + LANE_HIGH_OFFSET / 2.0;
    const float pixel_y = ImPlot::PlotToPixels(view.Min, middle).y;
    const float padding = ImGui::GetFontSize() * BIT_BOX_PADDING;
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const LogicSegment &cell : cells) {
        const double from = std::max(cell.From, view.Min);
        const double to = std::min(cell.To, view.Max);
        if (to <= from) {
            continue;
        }
        const char *text = GetLogicStateName(cell.State);
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const float left = ImPlot::PlotToPixels(from, middle).x;
        const float right = ImPlot::PlotToPixels(to, middle).x;
        const ImVec2 box_size = text_size + ImVec2(padding, padding) * 2.0f;
        if (right - left < box_size.x + padding) {
            continue;
        }
        const ImVec2 box_min((left + right - box_size.x) / 2.0f, pixel_y - box_size.y / 2.0f);
        draw_list->AddRectFilled(box_min, box_min + box_size, ImGui::GetColorU32(ImGuiCol_FrameBg), BIT_BOX_ROUNDING);
        draw_list->AddRect(box_min, box_min + box_size, color, BIT_BOX_ROUNDING);
        draw_list->AddText(box_min + ImVec2(padding, padding), ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    ImPlot::PopPlotClipRect();
}

/**
 * @brief   Draws one value of a bus between two pixel columns of its lane: a band whose ends slant in to its middle,
 *          shaded while the value is unknown, with the value written in it when it fits.
 */
void DrawBusValue(ImDrawList *draw_list, const float left, const float right, const float top, const float bottom,
                  const std::string &text, const bool is_unknown, const ImU32 color) {
    const float middle = (top + bottom) / 2.0f;
    const float slant = std::min(ImGui::GetFontSize() * BUS_SLANT, (right - left) / 2.0f);
    const std::array<ImVec2, 6> band = {ImVec2(left, middle),          ImVec2(left + slant, top),
                                        ImVec2(right - slant, top),    ImVec2(right, middle),
                                        ImVec2(right - slant, bottom), ImVec2(left + slant, bottom)};
    if (is_unknown) {
        ImVec4 fill = ImGui::ColorConvertU32ToFloat4(color);
        fill.w *= UNKNOWN_BUS_FILL_ALPHA;
        draw_list->AddConvexPolyFilled(band.data(), static_cast<int>(band.size()),
                                       ImGui::ColorConvertFloat4ToU32(fill));
    }
    draw_list->AddPolyline(band.data(), static_cast<int>(band.size()), color, ImDrawFlags_Closed, 1.5f);
    const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    // The text sits across the middle, where the band is widest, so it may reach into the slanted ends
    if (right - left >= text_size.x + slant) {
        draw_list->AddText({(left + right - text_size.x) / 2.0f, middle - text_size.y / 2.0f},
                           ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    }
}

/**
 * @brief   Draws the lane of a bus: one band per value within what the plot shows, so a value too short to see at the
 *          zoom is a sliver and its text is left out.
 */
void DrawBusLane(const Core::Transient &transient, const BusLane &lane, const std::size_t lane_position,
                 const ImU32 color) {
    const std::vector<LogicSignal> bits = GetBusSignals(lane, transient.NodeVoltages);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const double base = static_cast<double>(lane_position) * LANE_STRIDE;
    const float top = ImPlot::PlotToPixels(view.Min, base + LANE_HIGH_OFFSET).y;
    const float bottom = ImPlot::PlotToPixels(view.Min, base + LANE_LOW_OFFSET).y;
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const BusSegment &segment : FindBusSegments(transient.Times, bits)) {
        const double from = std::max(segment.From, view.Min);
        const double to = std::min(segment.To, view.Max);
        if (to <= from) {
            continue;
        }
        DrawBusValue(draw_list, ImPlot::PlotToPixels(from, base).x, ImPlot::PlotToPixels(to, base).x, top, bottom,
                     FormatBusValue(segment.Value, lane.Bits.size(), lane.Format), !segment.Value, color);
    }
    ImPlot::PopPlotClipRect();
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic whose transient is plotted; it must outlive the tab.
 * @param[in] controls   Runs of the analyses, to stop one from here while its plot grows; they must outlive the tab.
 * @param[in] playback   Instant of the transient shown on the schematic, drawn as a line that can be dragged, and the
 *                       window of points that plays; it must outlive the tab.
 */
TransientTab::TransientTab(Schematic &schematic, AnalysisControls &controls, TransientPlayback &playback)
    : m_Schematic(schematic), m_Controls(controls), m_Playback(playback) {
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
    DrawWithStatistics(
        [&] {
            if (const auto &steps = m_Schematic.GetTransientSteps()) {
                if (const std::optional<std::size_t> picked =
                        DrawStepSelector(steps->Parameter, steps->Unit, steps->Values, steps->Selected)) {
                    m_Schematic.SelectTransientStep(*picked);
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
                DrawPlots(shown, frame.TransientMath, frame.StepWindows);
            }
            ImGui::EndChild();
            if (has_playback) {
                DrawPlaybackBar(m_Playback, shown.Times.front(), shown.Times.back());
            }
        },
        [&] { DrawStatistics(*frame.TransientWindow, frame.TransientMath); });
}

/**
 * @brief   The span the statistics measure, which the spectrum transforms too.
 * @return  The span between the cursors, or what the plot shows, or the whole run before the plot was first drawn,
 *          clipped to the run from the earlier end; an empty span without a transient.
 */
PlotSpan TransientTab::GetMeasuredSpan() const {
    const auto &transient = m_Schematic.GetTransient();
    if (!transient || transient->Times.empty()) {
        return {};
    }
    const PlotSpan measured = MeasuredSpan(m_Cursors, m_View);
    const double run_start = transient->Times.front();
    const double run_stop = transient->Times.back();
    const double from = std::max(std::min(measured.From, measured.To), run_start);
    const double to = std::min(std::max(measured.From, measured.To), run_stop);
    if (!(from < to)) {
        return {.From = run_start, .To = run_stop};
    }
    return {.From = from, .To = to};
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
    if (const auto &transient = m_Schematic.GetTransient(); is_whole_run && transient) {
        static const std::vector<MathTrace> no_math;
        return BuildTransientFigure(*transient, frame.WholeRunMath != nullptr ? frame.WholeRunMath->Traces : no_math,
                                    FindMeasuredLanes(transient->NodeVoltages.size()),
                                    FindShownBuses(transient->NodeVoltages.size()), m_Schematic, m_ShowBitValues,
                                    {.From = transient->Times.front(), .To = transient->Times.back()});
    }
    if (frame.TransientWindow) {
        return BuildTransientFigure(
            *frame.TransientWindow, frame.TransientMath, FindMeasuredLanes(frame.TransientWindow->NodeVoltages.size()),
            FindShownBuses(frame.TransientWindow->NodeVoltages.size()), m_Schematic, m_ShowBitValues, m_View);
    }
    return {};
}

/**
 * @brief   A transient that has not reached its stop time is either still running and can be stopped from here, since
 *          the Output window takes the front as its first chunk arrives, or was stopped and keeps the points it made.
 * @details A live run has no stop time to reach: stopped, it is paused, and goes on or is dropped from here.
 */
void TransientTab::DrawIncompleteNote(const Core::Transient &transient) {
    if (m_Controls.IsLive()) {
        DrawLiveNote(transient);
        return;
    }
    const std::string reached = std::format("simulated up to {}s of {}s", Core::FormatValue(transient.Times.back()),
                                            Core::FormatValue(m_Schematic.GetSimulationSettings().Transient.StopTime));
    if (m_Controls.IsRunning()) {
        ImGui::BeginDisabled(m_Controls.IsStopping());
        if (ImGui::Button("Stop")) {
            m_Controls.StopRun();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            std::format("{}, {}", m_Controls.IsStopping() ? "Stopping" : "Running", reached).c_str());
    } else {
        ImGui::TextDisabled("%s", std::format("Stopped, {}", reached).c_str());
    }
}

/**
 * @brief   Stop pauses a live run, which then goes on with Continue or ends with Discard.
 */
void TransientTab::DrawLiveNote(const Core::Transient &transient) {
    const bool is_paused = m_Controls.IsLivePaused();
    ImGui::BeginDisabled(m_Controls.IsStopping());
    if (is_paused ? ImGui::Button("Continue") : ImGui::Button("Stop")) {
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
    ImGui::TextDisabled("%s", std::format("{}, simulated up to {}s", is_paused ? "Live, paused" : "Live",
                                          Core::FormatValue(transient.Times.back()))
                                  .c_str());
}

/**
 * @brief   A run longer than the plot shows is seen through a window of a size the user picks, which a scroll bar the
 *          width of the plot moves along the run, with a handle as large as the share of the run in view.
 * @details While the run goes on, the window can follow its newest points. The schematic plays the same window.
 */
void TransientTab::DrawWindowControls(const Core::Transient &shown) {
    TransientWindow &window = m_Playback.Window;
    const std::size_t total_points = window.GetTotalPoints();
    const bool has_span = window.GetSpan().has_value();
    if (shown.Times.empty()) {
        return;
    }
    // A run too short to cut into windows has no window size or scroll bar, only its range and the cursors
    const bool has_window = !((!has_span && total_points <= static_cast<std::size_t>(MIN_WINDOW_POINTS)) ||
                              (has_span && window.GetWindow().Count >= total_points));
    const float width = ImGui::GetContentRegionAvail().x;
    const std::string span =
        std::format("{}s to {}s", Core::FormatValue(shown.Times.front()), Core::FormatValue(shown.Times.back()));
    const ImVec2 row_start = ImGui::GetCursorPos();
    if (m_Controls.IsRunning()) {
        bool follows = window.DoesFollow();
        if (ImGui::Checkbox("Follow", &follows)) {
            window.SetFollow(follows);
        }
        ImGui::SetItemTooltip("Keep the window on the newest points of the run");
    }
    if (m_Controls.IsRunning()) {
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
    } else {
        ImGui::SetCursorPos(row_start);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Range");
    ImGui::SameLine();
    ImGui::TextUnformatted(span.c_str());
    // A live run is seen a stop time at a time, so its window has no size to pick
    const bool has_size = has_window && !has_span;
    const float size_width = has_size ? GetWindowSizeWidth() + ImGui::GetFontSize() : 0.0f;
    // At the right end of the row; in a narrow window, one after the other on rows of their own rather than over the
    // range
    ImGui::SameLine();
    const float right_edge = row_start.x + width;
    if (ImGui::GetCursorPosX() + ImGui::GetFontSize() + GetCursorToggleWidth() + size_width <= right_edge) {
        DrawCursorToggle(m_Cursors, right_edge - size_width);
        if (has_size) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(right_edge - GetWindowSizeWidth());
            DrawWindowSize();
        }
    } else {
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
    if (!has_window || window.GetWindow().Count >= total_points) {
        return;
    }
    const ImRect bar(ImGui::GetCursorScreenPos(),
                     ImGui::GetCursorScreenPos() + ImVec2(width, ImGui::GetStyle().ScrollbarSize));
    ImGui::ItemSize(bar);
    ImS64 first = static_cast<ImS64>(window.GetWindow().First);
    if (ImGui::ScrollbarEx(bar, ImGui::GetID("##window"), ImGuiAxis_X, &first,
                           static_cast<ImS64>(window.GetWindow().Count), static_cast<ImS64>(total_points))) {
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
    ImGui::TextDisabled("Points");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(size_width);
    int points = window.GetPoints();
    if (ImGui::DragInt("##window_points", &points, WINDOW_SIZE_DRAG_SPEED, MIN_WINDOW_POINTS, MAX_WINDOW_POINTS, "%d",
                       ImGuiSliderFlags_AlwaysClamp)) {
        window.SetPoints(points);
    }
    ImGui::SetItemTooltip("%s", std::format("How many points the plot and the schematic show at a time, from {} to "
                                            "{}; drag, or double-click to type. The run has {} points",
                                            MIN_WINDOW_POINTS, MAX_WINDOW_POINTS, window.GetTotalPoints())
                                    .c_str());
}

/**
 * @brief   The signals of logic parts are drawn as lanes in a plot under the analog traces, which share its time axis.
 * @details The analog plot is left out when it would be empty, so a circuit measured only at logic nodes shows the
 *          lanes alone.
 */
void TransientTab::DrawPlots(const Core::Transient &transient, const std::vector<MathTrace> &math,
                             const std::optional<StepFamily<Core::Transient>> &steps) {
    if (!transient.IsComplete) {
        DrawIncompleteNote(transient);
    }
    DrawWindowControls(transient);
    const std::vector<LogicNode> lanes = FindMeasuredLanes(transient.NodeVoltages.size());
    const std::vector<BusLane> buses = FindShownBuses(transient.NodeVoltages.size());
    const std::size_t lane_count = lanes.size() + buses.size();
    const std::vector<int> lane_nodes = ListNodes(lanes);
    std::vector<int> analog_nodes = ListMeasuredNodes(transient.NodeVoltages.size(), m_Schematic, lane_nodes);
    const bool has_voltages = !analog_nodes.empty() || UsesAxis(math, ImAxis_Y1);
    std::vector<std::string> measured_currents = ListMeasuredCurrents(transient.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool has_analog_plot = has_voltages || show_currents || !math.empty() || lane_count == 0;
    const bool currents_changed = m_Fit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed = m_Fit.TakeNodesChanged(std::move(analog_nodes));
    // A transient that is still being made fits its axes as the chunks arrive, so the plot follows the run; once the
    // run ends, whether whole or stopped, the axes are the user's again
    const bool has_grown = std::exchange(m_DrawnDataVersion, m_Schematic.GetTransientDataVersion()) !=
                               m_Schematic.GetTransientDataVersion() &&
                           !transient.IsComplete;
    // The window that moved is the one this frame's points come from, so the fit sees the points it moved to
    const PointWindow window = m_Playback.Window.GetWindow();
    const PointWindow previous = std::exchange(m_DrawnWindow, window);
    const bool has_moved = previous.First != window.First || previous.Count != window.Count;
    const bool new_result = m_Fit.TakeNewResult(m_Schematic.GetTransientVersion()) || has_grown || has_moved;
    SetNextFits(new_result, voltages_changed, currents_changed);
    if (m_Fit.TakeMathChanged(ListMathLabels(math))) {
        FitMathAxes(math);
    }
    float analog_height = PlotHeight(1);
    float lane_height = analog_height;
    if (lane_count > 0 && has_analog_plot) {
        const float font_size = ImGui::GetFontSize();
        const float available = ImGui::GetContentRegionAvail().y;
        lane_height = std::clamp(font_size * (LANE_PLOT_MARGIN + static_cast<float>(lane_count) * LANE_ROW_HEIGHT),
                                 font_size * MIN_PLOT_HEIGHT, available * MAX_LANE_SHARE);
        analog_height =
            std::max(font_size * MIN_PLOT_HEIGHT, available - lane_height - ImGui::GetStyle().ItemSpacing.y * 2.0f);
    }
    if (has_analog_plot) {
        DrawAnalog(transient, math, steps, lane_nodes, show_currents, has_voltages, lane_count > 0, analog_height);
    }
    if (lane_count > 0) {
        if (new_result) {
            ImPlot::SetNextAxisToFit(ImAxis_X1);
        }
        DrawLogicLanes(transient, buses, lanes, lane_height);
    }
}

void TransientTab::DrawAnalog(const Core::Transient &transient, const std::vector<MathTrace> &math,
                              const std::optional<StepFamily<Core::Transient>> &steps,
                              const std::vector<int> &lane_nodes, const bool show_currents, const bool has_voltages,
                              const bool has_lanes, const float height) {
    if (!BeginTracePlot("##transient", ImVec2(-1.0f, height))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents || !math.empty()));
    // The plot of the lanes below shares the time axis
    if (has_lanes) {
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    }
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    const std::string third_unit = ThirdAxisUnit(math);
    SetupMathAxes(math, show_currents, third_unit);
    std::vector<StepTraces> step_traces;
    if (steps) {
        for (const Core::Transient &step : steps->Results) {
            step_traces.push_back({&step.Times, &step.NodeVoltages, &step.Currents});
        }
        PlotOtherSteps(step_traces, steps->Selected, m_Schematic, show_currents, lane_nodes);
    }
    std::vector<PlottedTrace> plotted;
    PlotNodes(transient.Times, transient.NodeVoltages, m_Schematic, "V", "", plotted, lane_nodes);
    if (show_currents) {
        PlotCurrents(transient.Times, transient.Currents, m_Schematic, "A", "", plotted);
    }
    PlotMath(transient.Times, math, "", plotted);
    if (steps) {
        LabelSteps(step_traces, steps->Selected, ListStepLabels(steps->Parameter, steps->Unit, steps->Values),
                   m_Schematic, show_currents, lane_nodes);
    }
    DrawCursors(m_Cursors, false);
    DrawPlayhead(m_Playback, transient.Times.front(), transient.Times.back());
    DrawCursorReadout(transient.Times, "s", false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

/**
 * @brief   Each lane is a trace between its low and high levels, named on the axis, with a state read at the cursor.
 * @details A lane the circuit drives to a voltage between the thresholds is drawn halfway between low and high.
 */
void TransientTab::DrawLogicLanes(const Core::Transient &transient, const std::vector<BusLane> &buses,
                                  const std::vector<LogicNode> &lanes, const float height) {
    if (!ImPlot::BeginPlot("##transient_logic", ImVec2(-1.0f, height),
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        return;
    }
    // The buses take the top lanes and the signals the ones below, each lane at a height counted from the bottom
    const std::size_t lane_count = buses.size() + lanes.size();
    const auto position_of = [&](const std::size_t lane) { return lane_count - 1 - lane; };
    const auto top = static_cast<double>(lane_count - 1) * LANE_STRIDE + LANE_HIGH_OFFSET + LANE_MARGIN;
    ImPlot::SetupAxes("Time", "", ImPlotAxisFlags_None,
                      ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks);
    ImPlot::SetupAxisLimits(ImAxis_Y1, LANE_LOW_OFFSET - LANE_MARGIN, top, ImPlotCond_Always);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_View.From, &m_View.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    // The first lane is on top, and each is named at its middle
    std::vector<double> positions;
    std::vector<std::string> names;
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        positions.push_back(static_cast<double>(position_of(lane)) * LANE_STRIDE + LANE_HIGH_OFFSET / 2.0);
        names.push_back(lane < buses.size()
                            ? buses[lane].Name
                            : GetVoltageLabel(m_Schematic.GetConnectivity(), lanes[lane - buses.size()].Node));
    }
    std::vector<const char *> labels;
    for (const std::string &name : names) {
        labels.push_back(name.c_str());
    }
    ImPlot::SetupAxisTicks(ImAxis_Y1, positions.data(), static_cast<int>(positions.size()), labels.data());
    for (std::size_t bus = 0; bus < buses.size(); ++bus) {
        DrawBusLane(transient, buses[bus], position_of(bus), GetNodeColor(buses[bus].Bits.front().Node));
    }
    std::vector<PlottedTrace> plotted;
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const LogicNode &node = lanes[lane];
        const std::vector<double> &voltages = transient.NodeVoltages[static_cast<std::size_t>(node.Node)];
        const std::vector<double> heights = BuildLane(voltages, node.LogicLevel, position_of(buses.size() + lane));
        const ImU32 color = GetNodeColor(node.Node);
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        const std::string &name = names[buses.size() + lane];
        ImPlot::PlotLine(name.c_str(), transient.Times.data(), heights.data(), static_cast<int>(heights.size()), spec);
        plotted.push_back({name, &voltages, "V", color, node.LogicLevel});
    }
    if (m_ShowBitValues) {
        std::vector<LogicSignal> signals;
        for (const LogicNode &node : lanes) {
            signals.push_back({transient.NodeVoltages[static_cast<std::size_t>(node.Node)], node.LogicLevel});
        }
        const ImPlotRange view = ImPlot::GetPlotLimits().X;
        const auto cells = FindBitCells(transient.Times, signals, view.Min, view.Max);
        for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
            DrawBitValues(cells[lane], position_of(buses.size() + lane), GetNodeColor(lanes[lane].Node));
        }
    }
    DrawCursors(m_Cursors, false);
    DrawPlayhead(m_Playback, transient.Times.front(), transient.Times.back());
    DrawCursorReadout(transient.Times, "s", false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_View = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

/**
 * @brief   The logic signals the plot shows as lanes: the measured nodes of logic parts, unless lanes are turned off.
 */
std::vector<LogicNode> TransientTab::FindMeasuredLanes(const std::size_t node_count) const {
    if (!m_ShowLogicLanes) {
        return {};
    }
    return ListMeasuredLogicNodes(FindLogicNodes(m_Schematic.BuildCircuit()), node_count, m_Schematic);
}

/**
 * @brief   The buses the plot shows as lanes over the signals: those shown whose bits all name a node of the result,
 *          unless lanes are turned off.
 */
std::vector<BusLane> TransientTab::FindShownBuses(const std::size_t node_count) const {
    if (!m_ShowLogicLanes) {
        return {};
    }
    const NetResolver find_node = [this](const std::string_view net) {
        return m_Schematic.GetConnectivity().FindNodeOfNet(net);
    };
    return ResolveBusLanes(m_Schematic.GetBuses(), FindLogicNodes(m_Schematic.BuildCircuit()), find_node, node_count);
}

/**
 * @brief   Like the measurements of an oscilloscope, for every shown trace; with the cursors, also the value of each
 *          trace at both and their difference, and the time between them with its frequency.
 */
void TransientTab::DrawStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    const PlotCursors &cursors = m_Cursors;
    const std::vector<std::pair<std::string, std::string>> cursor_readings = {
        {"A", FormatQuantity(cursors.A, "s")},
        {"B", FormatQuantity(cursors.B, "s")},
        {"B-A", FormatQuantity(cursors.B - cursors.A, "s")},
        {"1/(B-A)", cursors.B != cursors.A ? FormatQuantity(1.0 / std::abs(cursors.B - cursors.A), "Hz") : "-"}};
    DrawStatisticsHeader(m_Cursors, cursors.Shown ? "Between the cursors" : "Over the visible time", cursor_readings);
    if (!FindLogicNodes(m_Schematic.BuildCircuit()).empty()) {
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
        BuildTransientStatistics(transient.Times, measured, MeasuredSpan(cursors, m_View),
                                 cursors.Shown ? std::optional(cursors) : std::nullopt);
    DrawTraceTable("trace_statistics", traces, rows);
}

} // namespace GUI
