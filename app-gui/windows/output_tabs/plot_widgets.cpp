/**
 * @file    plot_widgets.cpp
 * @brief   What the tabs of the Output window share to draw their plots and their statistics panels.
 */

#include "windows/output_tabs/plot_widgets.h"

#include "analysis/parameter_step.h"
#include "analysis/pole_zero_view.h"
#include "drawing/node_colors.h"
#include "drawing/theme.h"
#include "editing/probing.h"
#include "implot_internal.h"
#include "plots/logic_trace.h"
#include "plots/plot_figures.h"
#include "plots/plot_helpers.h"
#include "plots/trace_statistics.h"
#include "spice/spice_value.h"
#include "value_field.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

// Current dashes, in font sizes
constexpr float DASH_LENGTH = 0.5f;
constexpr float DASH_GAP = 0.3f;
constexpr float DASH_WEIGHT = 1.5f;
/// Width of the statistics panel, in font sizes.
constexpr float STATISTICS_WIDTH = 15.0f;
/// The lines that mark the poles and zeros on a Bode plot are dashed and thin, so they stay visible without hiding the
/// traces.
constexpr float ROOT_MARKER_ALPHA = 0.85f;
constexpr float ROOT_MARKER_DASH = 6.0f;
constexpr float ROOT_MARKER_GAP = 4.0f;
constexpr float ROOT_MARKER_WEIGHT = 1.5f;
/// Labels of the lines take turns in this many rows at the top of the plot.
constexpr int ROOT_LABEL_ROWS = 3;
/// Fraction of the visible range where cursor A is placed when turned on.
constexpr double CURSOR_A_PLACEMENT = 1.0 / 3.0;
/// Fraction of the visible range where cursor B is placed when turned on.
constexpr double CURSOR_B_PLACEMENT = 2.0 / 3.0;
constexpr ImU32 CURSOR_A_COLOR = IM_COL32(255, 199, 77, 255);
constexpr ImU32 CURSOR_B_COLOR = IM_COL32(115, 204, 255, 255);
/// The other steps of a stepped run are drawn this opaque, behind the shown step.
constexpr float OTHER_STEP_ALPHA = 0.35f;
/// Samples on each side of a step label over which the curves must stay apart.
constexpr std::size_t STEADY_SAMPLES = 2;

/**
 * @brief   A curve read at other X values, by straight lines between its samples and held at its ends; both X lists
 *          rise, so one walk along each does it.
 */
std::vector<double> ResampleOnto(const std::vector<double> &xs, const std::vector<double> &ys,
                                 const std::vector<double> &targets) {
    std::vector<double> values;
    values.reserve(targets.size());
    std::size_t index = 0;
    for (const double target : targets) {
        while (index + 1 < xs.size() && xs[index + 1] < target) {
            ++index;
        }
        if (xs.empty()) {
            values.push_back(0.0);
        } else if (target <= xs.front() || index + 1 >= xs.size()) {
            values.push_back(target <= xs.front() ? ys.front() : ys.back());
        } else {
            const double span = xs[index + 1] - xs[index];
            const double fraction = span > 0.0 ? (target - xs[index]) / span : 0.0;
            values.push_back(ys[index] + (ys[index + 1] - ys[index]) * fraction);
        }
    }
    return values;
}

/**
 * @brief   The sample where curves of equal length part most and stay apart for a few samples around it, so an edge
 *          that one step crosses before the others, as in a pulse, does not take the labels.
 */
std::size_t FindSteadySpread(const std::vector<std::vector<double>> &curves) {
    const std::size_t count = curves.empty() ? 0 : curves.front().size();
    std::vector<double> spreads(count, 0.0);
    for (std::size_t index = 0; index < count; ++index) {
        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
        for (const std::vector<double> &curve : curves) {
            if (std::isfinite(curve[index])) {
                lowest = std::min(lowest, curve[index]);
                highest = std::max(highest, curve[index]);
            }
        }
        spreads[index] = highest >= lowest ? highest - lowest : 0.0;
    }
    std::size_t widest = count == 0 ? 0 : count - 1;
    double widest_spread = -1.0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t first = index > STEADY_SAMPLES ? index - STEADY_SAMPLES : 0;
        const std::size_t last = std::min(index + STEADY_SAMPLES, count - 1);
        const double steady = *std::min_element(spreads.begin() + static_cast<std::ptrdiff_t>(first),
                                                spreads.begin() + static_cast<std::ptrdiff_t>(last) + 1);
        if (steady >= widest_spread) {
            widest_spread = steady;
            widest = index;
        }
    }
    return widest;
}

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
 * @brief   Begins a plot whose legend only names the traces: clicking an entry would hide a curve from the plot alone,
 *          while the statistics and the export still showed it.
 * @param[in] id    ImPlot ID of the plot.
 * @param[in] size  Size of the plot, in pixels.
 * @return  True when the plot is drawn; then call ImPlot::EndPlot().
 * @note    The trace list is the one place that picks what is shown.
 */
bool BeginTracePlot(const char *id, const ImVec2 size) {
    if (!ImPlot::BeginPlot(id, size)) {
        return false;
    }
    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);
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
 * @brief   Plots the other steps of a stepped run faint behind the shown one and without legend entries, so the family
 *          shows its spread while the shown step stays readable; currents are thin solid lines on their axis, since
 *          dashes would clutter a family.
 * @param[in] steps          Every step, as plotted.
 * @param[in] selected       The shown step, which is skipped.
 * @param[in] schematic      Which traces are measured.
 * @param[in] show_currents  Whether the plot has a current axis.
 * @param[in] lane_nodes     Nodes drawn as logic lanes, which are skipped as PlotNodes() skips them.
 * @note    Call it before plotting the shown step.
 */
void PlotOtherSteps(const std::vector<StepTraces> &steps, const std::size_t selected, const Schematic &schematic,
                    const bool show_currents, const std::vector<int> &lane_nodes) {
    for (std::size_t step = 0; step < steps.size(); ++step) {
        if (step == selected) {
            continue;
        }
        const StepTraces &traces = steps[step];
        const int count = static_cast<int>(traces.Xs->size());
        for (std::size_t node = 1; node < traces.Nodes->size(); ++node) {
            if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
                std::ranges::contains(lane_nodes, static_cast<int>(node))) {
                continue;
            }
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetNodeColor(static_cast<int>(node)));
            spec.LineColor.w = OTHER_STEP_ALPHA;
            ImPlot::PlotLine(std::format("##step{}v{}", step, node).c_str(), traces.Xs->data(),
                             (*traces.Nodes)[node].data(), count, spec);
        }
        if (!show_currents) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        for (std::size_t index = 0; index < traces.Currents->size(); ++index) {
            const Core::ComponentTrace &current = (*traces.Currents)[index];
            if (!schematic.IsCurrentMeasured(current.Name)) {
                continue;
            }
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
            spec.LineColor.w = OTHER_STEP_ALPHA;
            ImPlot::PlotLine(std::format("##step{}i{}", step, index).c_str(), traces.Xs->data(), current.Values.data(),
                             count, spec);
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
}

/**
 * @brief   Labels each step of every measured trace with its value, where its curves part most.
 * @param[in] steps          Every step, as plotted.
 * @param[in] selected       The shown step, at whose X values every step is read, since each transient has its own
 *                           time points.
 * @param[in] labels         The label of each step.
 * @param[in] schematic      Which traces are measured.
 * @param[in] show_currents  Whether the plot has a current axis.
 * @param[in] lane_nodes     Nodes drawn as logic lanes, which get no labels.
 * @note    A trace the steps leave unchanged, such as the input, gets no labels, which would only pile up on one line.
 */
void LabelSteps(const std::vector<StepTraces> &steps, const std::size_t selected,
                const std::vector<std::string> &labels, const Schematic &schematic, const bool show_currents,
                const std::vector<int> &lane_nodes) {
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    const std::vector<double> &xs = *steps[selected].Xs;
    const auto label_family = [&](const std::vector<const std::vector<double> *> &curves) {
        std::vector<std::vector<double>> resampled;
        double largest = 0.0;
        for (std::size_t step = 0; step < steps.size(); ++step) {
            resampled.push_back(ResampleOnto(*steps[step].Xs, *curves[step], xs));
            for (const double value : resampled.back()) {
                largest = std::max(largest, std::abs(value));
            }
        }
        const std::size_t sample = FindSteadySpread(resampled);
        if (sample >= xs.size()) {
            return;
        }
        const auto [lowest, highest] = std::ranges::minmax_element(
            resampled, {}, [sample](const std::vector<double> &curve) { return curve[sample]; });
        if ((*highest)[sample] - (*lowest)[sample] <= std::max(largest * NEGLIGIBLE_SPREAD, RESIDUE_LEVEL)) {
            return;
        }
        // Labels closer than a line of text to one already drawn are skipped, so none is drawn over another
        std::vector<float> drawn;
        for (std::size_t step = 0; step < steps.size(); ++step) {
            const double y = resampled[step][sample];
            const float pixel_y = ImPlot::PlotToPixels(xs[sample], y).y;
            if (std::ranges::any_of(
                    drawn, [pixel_y](const float other) { return std::abs(other - pixel_y) < ImGui::GetFontSize(); })) {
                continue;
            }
            drawn.push_back(pixel_y);
            ImPlot::Annotation(xs[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s", labels[step].c_str());
        }
    };
    const StepTraces &first = steps.front();
    for (std::size_t node = 1; node < first.Nodes->size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
            std::ranges::contains(lane_nodes, static_cast<int>(node))) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const StepTraces &traces : steps) {
            curves.push_back(&(*traces.Nodes)[node]);
        }
        label_family(curves);
    }
    if (!show_currents) {
        return;
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    for (std::size_t index = 0; index < first.Currents->size(); ++index) {
        if (!schematic.IsCurrentMeasured((*first.Currents)[index].Name)) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const StepTraces &traces : steps) {
            curves.push_back(&(*traces.Currents)[index].Values);
        }
        label_family(curves);
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

/**
 * @brief   Picks the step the statistics, the spectrum and the schematic read.
 * @param[in] parameter  Name of the stepped parameter.
 * @param[in] unit       Unit of its values.
 * @param[in] values     The value of each step.
 * @param[in] selected   The step shown now.
 * @return  The step the user picked this frame, or no value.
 */
std::optional<std::size_t> DrawStepSelector(const std::string &parameter, const std::string &unit,
                                            const std::span<const double> values, const std::size_t selected) {
    std::optional<std::size_t> picked;
    DrawFieldLabel("Shown step");
    const std::string shown = FormatStepValue(parameter, values[selected], unit);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##shown_step", shown.c_str())) {
        for (std::size_t step = 0; step < values.size(); ++step) {
            const std::string label = FormatStepValue(parameter, values[step], unit);
            if (ImGui::Selectable(label.c_str(), step == selected)) {
                picked = step;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The statistics, the spectrum and the schematic read this step; the others are faint");
    return picked;
}

/**
 * @brief   The labels of the steps of a stepped run, such as "R1 = 1k".
 * @param[in] parameter  Name of the stepped parameter.
 * @param[in] unit       Unit of its values.
 * @param[in] values     The value of each step.
 * @return  One label per step.
 */
std::vector<std::string> ListStepLabels(const std::string &parameter, const std::string &unit,
                                        const std::span<const double> values) {
    std::vector<std::string> labels;
    for (const double value : values) {
        labels.push_back(FormatStepValue(parameter, value, unit));
    }
    return labels;
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
 * @brief   The checkbox that shows the cursors; turning them on places them again in the current view, so they are
 *          never lost off screen.
 * @param[in,out] cursors  The cursors of the plot.
 */
void DrawCursorToggle(PlotCursors &cursors) {
    if (ImGui::Checkbox("Cursors A and B", &cursors.Shown) && cursors.Shown) {
        cursors.Placed = false;
    }
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
 * @brief   Writes a measured value with four significant digits, a SPICE suffix and its unit, such as "4.700kOhm".
 */
std::string FormatQuantity(const double value, const char *unit) {
    return std::format("{}{}", Core::FormatMeasurement(value), unit);
}

/**
 * @brief   Writes a value with two decimals and its unit: decibels and degrees read better with fixed decimals than
 *          with SPICE suffixes, which would show 500m dB.
 */
std::string FormatFixed(const double value, const char *unit) {
    return Core::FormatFixed(value, 2) + unit;
}

/**
 * @brief   Writes a value with its suffix and unit, or "-" for no value.
 */
std::string FormatOptionalQuantity(const std::optional<double> value, const char *unit) {
    return value ? FormatQuantity(*value, unit) : "-";
}

/**
 * @brief   Draws a two-column row of a statistics table: what is measured and its value.
 */
void DrawStatistic(const char *name, const std::string &value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
}

/**
 * @brief   Draws a row with a value; a negligible value reads as ~0, so it is not mistaken for an exact zero, and
 *          hovering it shows the exact value.
 * @param[in] name   What is measured.
 * @param[in] value  The value.
 * @param[in] scale  The size of the trace, which tells residue from real values.
 * @param[in] unit   Unit of the value.
 */
void DrawMeasuredValue(const char *name, const double value, const double scale, const char *unit) {
    if (!IsNegligible(value, scale)) {
        DrawStatistic(name, FormatQuantity(value, unit));
        return;
    }
    DrawStatistic(name, std::format("~0{}", unit));
    ImGui::SetItemTooltip(
        "%s", std::format("{}{}, negligible next to the trace", Core::FormatMeasurement(value), unit).c_str());
}

/**
 * @brief   Begins a two-column table of statistics.
 * @param[in] id  ImGui ID of the table, such as the label of its trace.
 * @return  True when the table is drawn; then call ImGui::EndTable().
 */
bool BeginStatisticsTable(const std::string &id) {
    return ImGui::BeginTable(id.c_str(), 2, ImGuiTableFlags_SizingStretchProp);
}

/**
 * @brief   Splits a tab into the plots and the statistics panel on their right.
 * @param[in] draw_plots       Draws the plots, in the left column.
 * @param[in] draw_statistics  Draws the statistics, in the bordered panel on the right.
 */
void DrawWithStatistics(const std::function<void()> &draw_plots, const std::function<void()> &draw_statistics) {
    const float statistics_width = ImGui::GetFontSize() * STATISTICS_WIDTH;
    if (ImGui::BeginChild("plots_column", ImVec2(-statistics_width - ImGui::GetStyle().ItemSpacing.x, 0.0f))) {
        draw_plots();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("statistics", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        draw_statistics();
    }
    ImGui::EndChild();
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
