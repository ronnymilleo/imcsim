/**
 * @file    plot_widgets.cpp
 * @brief   What the tabs of the Output window share to draw their plots: axes, traces, math channels and the markers of
 * poles and zeros.
 */

#include "windows/output_tabs/plot_widgets.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "plots/plot_helpers.h"
#include "results/logic_trace.h"
#include "results/pole_zero_view.h"
#include "schematic/probing.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

namespace GUI {

namespace {

// Current dashes, in font sizes
constexpr float DASH_LENGTH = 0.5f;
constexpr float DASH_GAP = 0.3f;
constexpr float DASH_WEIGHT = 1.5f;
/// The lines that mark the poles and zeros on a Bode plot are dashed and thin, so they stay visible without hiding the
/// traces.
constexpr float ROOT_MARKER_ALPHA = 0.85f;
constexpr float ROOT_MARKER_DASH = 6.0f;
constexpr float ROOT_MARKER_GAP = 4.0f;
constexpr float ROOT_MARKER_WEIGHT = 1.5f;
/// Labels of the lines take turns in this many rows at the top of the plot.
constexpr int ROOT_LABEL_ROWS = 3;

} // namespace

/**
 * @brief   Formats axis ticks with the same suffixes as the values the user types, such as "10m" or "1k", followed by
 *          the unit.
 * @param[in]  value   The value of the tick.
 * @param[out] buffer  Where the text goes.
 * @param[in]  size    Room in the buffer.
 * @param[in]  unit    The unit, as a C string.
 * @return  The length of the text, as snprintf() returns it.
 * @note    ImPlot hands the unit back as untyped user data, which is why callers cast away the const of the literal.
 */
int FormatAxisValue(const double value, char *buffer, const int size, void *unit) {
    const std::string text = std::format("{}{}", Core::FormatValue(value), static_cast<const char *>(unit));
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s", text.c_str());
}

/**
 * @brief   Begins a plot of traces, without a legend when a list beside it shows the color and style of each trace, so
 *          nothing covers the curves.
 * @param[in] id          ImPlot ID of the plot.
 * @param[in] size        Size of the plot, in pixels.
 * @param[in] has_legend  True for a plot whose traces no list shows, such as the noise of each part; its legend only
 *                        names them, since clicking an entry would hide a curve from the plot alone while the
 *                        statistics and the export still showed it.
 * @return  True when the plot is drawn; then call ImPlot::EndPlot().
 * @note    The trace list is the one place that picks what is shown.
 */
bool BeginTracePlot(const char *id, const ImVec2 size, const bool has_legend) {
    if (!ImPlot::BeginPlot(id, size, has_legend ? ImPlotFlags_None : ImPlotFlags_NoLegend)) {
        return false;
    }
    if (has_legend) {
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);
    }
    return true;
}

/**
 * @brief   Splits the room left in the window between the plots.
 * @param[in] plot_count  How many plots share it.
 * @return  The height of each, never under MIN_PLOT_HEIGHT font sizes.
 */
float PlotHeight(const int plot_count) {
    return std::max(ImGui::GetFontSize() * MIN_PLOT_HEIGHT,
                    ImGui::GetContentRegionAvail().y / static_cast<float>(plot_count) -
                        ImGui::GetStyle().ItemSpacing.y);
}

/**
 * @brief   While other traces are shown and no voltage is, the voltage axis keeps its grid but drops its label and
 *          ticks, so an empty scale is not read as data.
 * @param[in] has_voltages      Whether the plot shows a voltage.
 * @param[in] has_other_traces  Whether it shows currents or math channels.
 * @return  The flags of the voltage axis.
 */
ImPlotAxisFlags VoltageAxisFlags(const bool has_voltages, const bool has_other_traces) {
    if (has_voltages || !has_other_traces) {
        return ImPlotAxisFlags_None;
    }
    return ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels;
}

/**
 * @brief   Fits every axis for a new result; otherwise only the axes whose traces changed, so the zoom of the others
 *          stays.
 * @param[in] new_result        Whether the result is new.
 * @param[in] voltages_changed  Whether the measured voltages changed.
 * @param[in] currents_changed  Whether the measured currents changed.
 * @note    Call it before each BeginPlot().
 */
void SetNextFits(const bool new_result, const bool voltages_changed, const bool currents_changed) {
    if (new_result) {
        ImPlot::SetNextAxesToFit();
        return;
    }
    if (voltages_changed) {
        ImPlot::SetNextAxisToFit(ImAxis_Y1);
    }
    if (currents_changed) {
        ImPlot::SetNextAxisToFit(ImAxis_Y2);
    }
}

/**
 * @brief   Plots the measured node voltages; ground is always 0 V, so plots start at node 1.
 * @param[in]     xs           X values of the samples.
 * @param[in]     node_values  Values of every node, by node number.
 * @param[in]     schematic    Which nodes are measured, and the names of nets.
 * @param[in]     unit         Unit of the values, for the cursor readout.
 * @param[in]     suffix       Tells the curves of a DC sweep family apart in the cursor readout.
 * @param[in,out] plotted      The traces drawn, for the cursor readout.
 * @param[in]     excluded     Nodes drawn elsewhere, such as logic lanes.
 * @note    Each node keeps its color whichever nodes are measured.
 */
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values, Schematic &schematic,
               const char *unit, const std::string &suffix, std::vector<PlottedTrace> &plotted,
               const std::vector<int> &excluded) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
            std::ranges::contains(excluded, static_cast<int>(node))) {
            continue;
        }
        const ImU32 color = GetNodeColor(static_cast<int>(node));
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node));
        ImPlot::PlotLine(label.c_str(), xs.data(), node_values[node].data(), static_cast<int>(xs.size()), spec);
        plotted.push_back({label + suffix, &node_values[node], unit, color});
    }
}

/**
 * @brief   Plots a dashed line: ImPlot draws no dashes, so the line is drawn by hand, a dummy item gives the legend
 *          entry, and an invisible line keeps the automatic fit of the axes.
 * @param[in] label  Legend entry.
 * @param[in] xs     X values of the samples.
 * @param[in] ys     Y values of the samples.
 * @param[in] color  Color of the line.
 */
void PlotDashedLine(const std::string &label, const std::vector<double> &xs, const std::vector<double> &ys,
                    const ImU32 color) {
    ImPlotSpec legend_spec;
    legend_spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
    ImPlot::PlotDummy(label.c_str(), legend_spec);
    ImPlotSpec fit_spec;
    fit_spec.LineColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImPlot::PlotLine(std::format("##{}", label).c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), fit_spec);

    // Samples closer than a pixel to the last one add nothing, and long transients have many of them
    std::vector<ImVec2> points;
    for (std::size_t index = 0; index < xs.size(); ++index) {
        const ImVec2 point = ImPlot::PlotToPixels(xs[index], ys[index]);
        if (points.empty() || std::abs(point.x - points.back().x) >= 1.0f ||
            std::abs(point.y - points.back().y) >= 1.0f || index + 1 == xs.size()) {
            points.push_back(point);
        }
    }
    const float font_size = ImGui::GetFontSize();
    const ImVec2 plot_min = ImPlot::GetPlotPos();
    const ImVec2 plot_max = plot_min + ImPlot::GetPlotSize();
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const LineSegment &dash :
         SplitIntoDashes(points, DASH_LENGTH * font_size, DASH_GAP * font_size, plot_min, plot_max)) {
        draw_list->AddLine(dash.Start, dash.End, color, DASH_WEIGHT);
    }
    ImPlot::PopPlotClipRect();
}

/**
 * @brief   Plots the measured currents dashed on the secondary Y axis, which the caller sets up before plotting
 *          anything.
 * @param[in]     xs         X values of the samples.
 * @param[in]     currents   Every current of the result.
 * @param[in]     schematic  Which currents are measured.
 * @param[in]     unit       Unit of the values, for the cursor readout.
 * @param[in]     suffix     Tells the curves of a DC sweep family apart in the cursor readout.
 * @param[in,out] plotted    The traces drawn, for the cursor readout.
 * @note    Each keeps the color of its position in the circuit, as in the editor.
 */
void PlotCurrents(const std::vector<double> &xs, const std::vector<Core::ComponentTrace> &currents,
                  const Schematic &schematic, const char *unit, const std::string &suffix,
                  std::vector<PlottedTrace> &plotted) {
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (!schematic.IsCurrentMeasured(currents[index].Name)) {
            continue;
        }
        const std::string label = std::format("I({})", currents[index].Name);
        PlotDashedLine(label, xs, currents[index].Values, GetCurrentColor(index));
        plotted.push_back({label + suffix, &currents[index].Values, unit, GetCurrentColor(index)});
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

/**
 * @brief   Tells whether a node of a result other than ground is measured.
 * @param[in] node_count  Nodes of the result.
 * @param[in] schematic   Which nodes are measured.
 * @return  True when a plot of the result shows a voltage.
 */
bool HasMeasuredVoltage(const std::size_t node_count, const Schematic &schematic) {
    for (std::size_t node = 1; node < node_count; ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            return true;
        }
    }
    return false;
}

/**
 * @brief   Lists the measured nodes of a result, ground excluded.
 * @param[in] node_count  Nodes of the result.
 * @param[in] schematic   Which nodes are measured.
 * @param[in] excluded    Nodes left out, such as logic lanes.
 * @return  The node numbers; empty when the plot shows no voltage.
 */
std::vector<int> ListMeasuredNodes(const std::size_t node_count, const Schematic &schematic,
                                   const std::vector<int> &excluded) {
    std::vector<int> nodes;
    for (std::size_t node = 1; node < node_count; ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node)) &&
            !std::ranges::contains(excluded, static_cast<int>(node))) {
            nodes.push_back(static_cast<int>(node));
        }
    }
    return nodes;
}

/**
 * @brief   Lists the measured currents of a result, in result order.
 * @param[in] currents   Every current of the result.
 * @param[in] schematic  Which currents are measured.
 * @return  Their names; empty when the plot shows no current axis.
 */
std::vector<std::string> ListMeasuredCurrents(const std::vector<Core::ComponentTrace> &currents,
                                              const Schematic &schematic) {
    std::vector<std::string> names;
    for (const Core::ComponentTrace &current : currents) {
        if (schematic.IsCurrentMeasured(current.Name)) {
            names.push_back(current.Name);
        }
    }
    return names;
}

/**
 * @brief   Every measured trace of a result, voltages first, with the labels and colors of the plots.
 * @param[in] node_values   Values of every node, by node number.
 * @param[in] currents      Every current of the result.
 * @param[in] schematic     Which traces are measured, and the names of nets.
 * @param[in] voltage_unit  Unit of the voltages.
 * @param[in] current_unit  Unit of the currents.
 * @return  The traces, pointing into the result, which must outlive them.
 */
std::vector<PlottedTrace> ListTraces(const std::vector<std::vector<double>> &node_values,
                                     const std::vector<Core::ComponentTrace> &currents, Schematic &schematic,
                                     const char *voltage_unit, const char *current_unit) {
    std::vector<PlottedTrace> traces;
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            traces.push_back({GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node)), &node_values[node],
                              voltage_unit, GetNodeColor(static_cast<int>(node))});
        }
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (schematic.IsCurrentMeasured(currents[index].Name)) {
            traces.push_back({std::format("I({})", currents[index].Name), &currents[index].Values, current_unit,
                              GetCurrentColor(index)});
        }
    }
    return traces;
}

/**
 * @brief   Sets up the axes the math channels need besides the voltage axis.
 * @param[in] math                The math channels of the plot.
 * @param[in] current_axis_ready  Whether the current axis is already set up for probed currents.
 * @param[in] third_unit          Unit of the third axis, which must outlive the plot because ImPlot formats the ticks
 *                                when it ends.
 */
void SetupMathAxes(const std::vector<MathTrace> &math, const bool current_axis_ready, const std::string &third_unit) {
    if (!current_axis_ready && UsesAxis(math, ImAxis_Y2)) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    if (UsesAxis(math, ImAxis_Y3)) {
        ImPlot::SetupAxis(ImAxis_Y3, third_unit.empty() ? "Math" : third_unit.c_str(), ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y3, FormatAxisValue, const_cast<char *>(third_unit.c_str()));
    }
}

/**
 * @brief   Fits the axes of the math traces when the channels change, like the probed traces.
 * @param[in] math  The math channels of the plot.
 */
void FitMathAxes(const std::vector<MathTrace> &math) {
    for (const ImAxis axis : {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3}) {
        if (UsesAxis(math, axis)) {
            ImPlot::SetNextAxisToFit(axis);
        }
    }
}

/**
 * @brief   Plots the math channels on their axes; division by zero leaves NaN samples, which are skipped instead of
 *          breaking the line.
 * @param[in]     xs            X values of the samples.
 * @param[in]     math          The math channels.
 * @param[in]     suffix        Tells the curves of a DC sweep family apart in the cursor readout.
 * @param[in,out] plotted       The traces drawn, for the cursor readout.
 * @param[in]     readout_unit  Unit the cursor readout gives the values, such as " dB" for the magnitudes of an AC
 *                              sweep, or null for the unit of each channel.
 */
void PlotMath(const std::vector<double> &xs, const std::vector<MathTrace> &math, const std::string &suffix,
              std::vector<PlottedTrace> &plotted, const char *readout_unit) {
    for (const MathTrace &trace : math) {
        ImPlot::SetAxes(ImAxis_X1, trace.Axis);
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
        spec.LineWeight = MATH_LINE_WEIGHT;
        spec.Flags = ImPlotLineFlags_SkipNaN;
        ImPlot::PlotLine(trace.Label.c_str(), xs.data(), trace.Values.data(), static_cast<int>(xs.size()), spec);
        plotted.push_back({trace.Label + suffix, &trace.Values,
                           readout_unit != nullptr ? readout_unit : trace.Unit.c_str(), trace.Color});
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

/**
 * @brief   Shows why math channels could not be worked out, in the warning color.
 * @param[in] errors  One line per channel that failed.
 */
void DrawMathErrors(const std::vector<std::string> &errors) {
    for (const std::string &error : errors) {
        ImGui::TextColored(GetWarningTextColor(), "%s", error.c_str());
    }
}

/**
 * @brief   The color of a pole or a zero, which the s-plane and the Bode plot markers share.
 * @param[in] is_pole              True for a pole, false for a zero.
 * @param[in] is_right_half_plane  True for a root with a positive real part, which takes the warning color.
 * @return  The plot colors one and two for the left half-plane, so a pole and a zero differ.
 */
ImVec4 GetPoleZeroColor(const bool is_pole, const bool is_right_half_plane) {
    if (is_right_half_plane) {
        return GetWarningTextColor();
    }
    return ImPlot::GetColormapColor(is_pole ? 0 : 1);
}

/**
 * @brief   Draws a dashed vertical line at the natural frequency of every pole and zero, over a plot with a frequency
 * in Hz as its X axis, colored as in the s-plane.
 * @param[in] result      The pole-zero result.
 * @param[in] has_labels  Whether each line also has its kind and frequency, such as "p 1.07k", at the top of the plot.
 * @note    Call it inside a plot. Roots at 0 Hz have no place on a logarithmic axis and are left out. The labels are
 *          staggered in height, so lines close together do not hide each other's.
 */
void DrawRootMarkers(const Core::PoleZero &result, const bool has_labels) {
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    const ImVec2 corner = ImPlot::GetPlotPos();
    const ImVec2 size = ImPlot::GetPlotSize();
    const float line_height = ImGui::GetTextLineHeight();
    ImPlot::PushPlotClipRect();
    int drawn = 0;
    for (const bool is_pole : {true, false}) {
        for (const bool is_right_half_plane : {false, true}) {
            ImVec4 color = GetPoleZeroColor(is_pole, is_right_half_plane);
            color.w = ROOT_MARKER_ALPHA;
            const ImU32 line_color = ImGui::ColorConvertFloat4ToU32(color);
            for (const double frequency : ListRootFrequencies(result, is_pole, is_right_half_plane)) {
                const float x = ImPlot::PlotToPixels(frequency, 0.0).x;
                if (x < corner.x || x > corner.x + size.x) {
                    continue;
                }
                const int dash_count = static_cast<int>(size.y / (ROOT_MARKER_DASH + ROOT_MARKER_GAP)) + 1;
                for (int dash = 0; dash < dash_count; ++dash) {
                    const float y = corner.y + static_cast<float>(dash) * (ROOT_MARKER_DASH + ROOT_MARKER_GAP);
                    draw_list->AddLine({x, y}, {x, std::min(y + ROOT_MARKER_DASH, corner.y + size.y)}, line_color,
                                       ROOT_MARKER_WEIGHT);
                }
                if (has_labels) {
                    const float label_y = corner.y + 2.0f + static_cast<float>(drawn % ROOT_LABEL_ROWS) * line_height;
                    const std::string label = std::format("{} {}", is_pole ? "p" : "z", Core::FormatValue(frequency));
                    draw_list->AddText({x + 3.0f, label_y}, line_color, label.c_str());
                }
                ++drawn;
            }
        }
    }
    ImPlot::PopPlotClipRect();
}

/**
 * @brief   Toggles every trace of a section at once; the ID keeps the buttons of each section apart.
 * @param[in] id  ImGui ID of the section.
 * @return  The new state for all of them, or no value when neither button was pressed.
 */
std::optional<bool> DrawAllNoneButtons(const char *id) {
    ImGui::PushID(id);
    std::optional<bool> measure_all;
    if (ImGui::SmallButton("All")) {
        measure_all = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        measure_all = false;
    }
    ImGui::PopID();
    return measure_all;
}

} // namespace GUI
