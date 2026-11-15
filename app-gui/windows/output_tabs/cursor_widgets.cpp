/**
 * @file    cursor_widgets.cpp
 * @brief   The measurement cursors of the plots of the Output window, their readout and the playback bar of a
 * transient.
 */

#include "windows/output_tabs/cursor_widgets.h"

#include "base/theme.h"
#include "plots/plot_helpers.h"
#include "results/logic_trace.h"
#include "results/trace_statistics.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace GUI {

namespace {

/// The time slider of a transient does not shrink below this, in font sizes, so it stays usable in a narrow window.
constexpr float MIN_TIME_SLIDER_WIDTH = 5.0f;
/// The instant readout keeps the width of its widest text, so the slider does not resize as the digits change: four
/// significant digits with a suffix are at most this wide.
constexpr const char *WIDEST_TIME_READOUT = "t = 888.8ms / 888.8ms";
/// Fraction of the visible range where cursor A is placed when turned on.
constexpr double CURSOR_A_PLACEMENT = 1.0 / 3.0;
/// Fraction of the visible range where cursor B is placed when turned on.
constexpr double CURSOR_B_PLACEMENT = 2.0 / 3.0;
constexpr ImU32 CURSOR_A_COLOR = IM_COL32(255, 199, 77, 255);
constexpr ImU32 CURSOR_B_COLOR = IM_COL32(115, 204, 255, 255);

} // namespace

/**
 * @brief   While the plot is hovered, marks the sample nearest the cursor and lists every plotted trace at it.
 * @param[in] xs           X values of the samples.
 * @param[in] x_unit       Unit of the X axis.
 * @param[in] logarithmic  Whether the X axis is logarithmic, which changes what is nearest.
 * @param[in] plotted      The traces drawn.
 */
void DrawCursorReadout(const std::vector<double> &xs, const char *x_unit, const bool logarithmic,
                       const std::vector<PlottedTrace> &plotted) {
    if (!ImPlot::IsPlotHovered() || xs.empty() || plotted.empty()) {
        return;
    }
    const std::size_t sample = FindNearestSample(xs, ImPlot::GetPlotMousePos().x, logarithmic);
    const float x = ImPlot::PlotToPixels(xs[sample], 0.0).x;
    const ImVec2 plot_position = ImPlot::GetPlotPos();
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddLine({x, plot_position.y}, {x, plot_position.y + ImPlot::GetPlotSize().y},
                                       ImGui::GetColorU32(ImGuiCol_Text, 0.35f));
    ImPlot::PopPlotClipRect();

    ImGui::BeginTooltip();
    ImGui::TextDisabled("%s", std::format("at {}{}", Core::FormatValue(xs[sample]), x_unit).c_str());
    for (const PlottedTrace &trace : plotted) {
        if (trace.Values == nullptr || sample >= trace.Values->size()) {
            continue;
        }
        const double value = (*trace.Values)[sample];
        std::string text = std::format("{}: {}{}", trace.Label, Core::FormatMeasurement(value), trace.Unit);
        if (trace.LogicLevel) {
            text =
                std::format("{}: {} ({}{})", trace.Label, GetLogicStateName(ReadLogicState(value, *trace.LogicLevel)),
                            Core::FormatMeasurement(value), trace.Unit);
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", text.c_str());
    }
    ImGui::EndTooltip();
}

/**
 * @brief   Draws the cursors in the current plot, placing them in the visible range the first frame they are shown.
 * @param[in,out] cursors      The cursors of the plot.
 * @param[in]     logarithmic  Whether the X axis is logarithmic, where they are placed by decades, as it shows them.
 */
void DrawCursors(PlotCursors &cursors, const bool logarithmic) {
    if (!cursors.Shown) {
        return;
    }
    if (!std::exchange(cursors.Placed, true)) {
        const ImPlotRange range = ImPlot::GetPlotLimits().X;
        const auto place = [&range, logarithmic](const double fraction) {
            if (logarithmic) {
                return std::pow(10.0, std::lerp(std::log10(range.Min), std::log10(range.Max), fraction));
            }
            return std::lerp(range.Min, range.Max, fraction);
        };
        cursors.A = place(CURSOR_A_PLACEMENT);
        cursors.B = place(CURSOR_B_PLACEMENT);
    }
    const ImVec4 color_a = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(CURSOR_A_COLOR));
    const ImVec4 color_b = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(CURSOR_B_COLOR));
    ImPlot::DragLineX(0, &cursors.A, color_a, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.A, color_a, "A");
    ImPlot::DragLineX(1, &cursors.B, color_b, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.B, color_b, "B");
}

/**
 * @brief   The span the statistics cover.
 * @param[in] cursors  The cursors of the plot.
 * @param[in] view     What the plot shows.
 * @return  The span between the cursors while they are shown, otherwise what the plot shows.
 */
PlotSpan MeasuredSpan(const PlotCursors &cursors, const PlotSpan &view) {
    return cursors.Shown ? PlotSpan{.From = cursors.A, .To = cursors.B} : view;
}

/**
 * @brief   The width the cursor checkbox takes, for a header that places it at its right end.
 * @return  The width in pixels.
 */
float GetCursorToggleWidth() {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Cursors").x;
}

/**
 * @brief   Draws the checkbox of cursors A and B on the current row of the plot header, ending at an edge; checking it
 *          places them anew in the view.
 * @param[in,out] cursors     Cursors of the plot.
 * @param[in]     right_edge  Where the checkbox ends, in window coordinates.
 */
void DrawCursorToggle(PlotCursors &cursors, const float right_edge) {
    // On the row when it fits after what is there, on a row of its own in a narrow window
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() + GetCursorToggleWidth() > right_edge) {
        ImGui::NewLine();
    }
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_edge - GetCursorToggleWidth()));
    DrawCursorCheckbox(cursors);
}

/**
 * @brief   Draws the checkbox of cursors A and B at the cursor; checking it places them anew in the view.
 * @param[in,out] cursors  Cursors of the plot.
 */
void DrawCursorCheckbox(PlotCursors &cursors) {
    if (ImGui::Checkbox("Cursors", &cursors.Shown) && cursors.Shown) {
        cursors.Placed = false;
    }
    ImGui::SetItemTooltip("Measure between two cursors, A and B, dragged on the plot");
}

/**
 * @brief   Draws the playback of the transient under its plot: play, loop, a slider over the window of points the plot
 *          shows and the instant, which the schematic and the playhead of the plot follow.
 * @param[in,out] playback  The instant shown and whether it plays.
 * @param[in]     from      Start of the window, in seconds.
 * @param[in]     to        End of the window, in seconds.
 * @note    Moving the slider stops the playback, so it holds where the user put it.
 */
void DrawPlaybackBar(TransientPlayback &playback, const double from, const double to) {
    const ImGuiStyle &style = ImGui::GetStyle();
    // Play and Pause share the width of the wider label, so the row does not shift when they swap
    const float play_width =
        std::max(ImGui::CalcTextSize("Play").x, ImGui::CalcTextSize("Pause").x) + style.FramePadding.x * 2.0f;
    if (ImGui::Button(playback.Playing ? "Pause" : "Play", ImVec2(play_width, 0.0f))) {
        if (!playback.Playing && playback.Time >= to) {
            playback.Time = from;
        }
        playback.Playing = !playback.Playing;
    }
    ImGui::SetItemTooltip("Play the window of the transient on the schematic");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &playback.Loop);
    ImGui::SameLine();
    const std::string readout = std::format("t = {}s / {}s", Core::FormatValue(playback.Time), Core::FormatValue(to));
    const float readout_width =
        std::max(ImGui::CalcTextSize(readout.c_str()).x, ImGui::CalcTextSize(WIDEST_TIME_READOUT).x) +
        style.ItemSpacing.x;
    ImGui::SetNextItemWidth(
        std::max(ImGui::GetContentRegionAvail().x - readout_width, ImGui::GetFontSize() * MIN_TIME_SLIDER_WIDTH));
    ImGui::SliderScalar("##time", ImGuiDataType_Double, &playback.Time, &from, &to, "");
    if (ImGui::IsItemActive()) {
        playback.Playing = false;
    }
    ImGui::SetItemTooltip("Instant of the transient shown on the schematic and by the playhead of the plot");
    ImGui::SameLine();
    ImGui::TextUnformatted(readout.c_str());
}

} // namespace GUI
