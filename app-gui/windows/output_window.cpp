/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#include "output_window.h"

#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "logic_trace.h"
#include "node_colors.h"
#include "plot_figures.h"
#include "plot_helpers.h"
#include "probing.h"
#include "spice_value.h"
#include "theme.h"
#include "trace_statistics.h"
#include "value_field.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GUI {

namespace {

// Section of imgui.ini that keeps the View preference of the window
constexpr const char *SettingsTypeName = "Output";

// In font sizes, so the layout follows the DPI scale
constexpr float MinPlotHeight = 12.0f;
constexpr float TraceListWidth = 8.0f;
constexpr ImVec2 InitialSize = {65.0f, 35.0f};
// ImPlot adds half of it on each side, so 0.4 leaves 20% of the data range above and below the curves
constexpr ImVec2 FitPadding = {0.0f, 0.4f};
// Current dashes, in font sizes
constexpr float DashLength = 0.5f;
constexpr float DashGap = 0.3f;
constexpr float DashWeight = 1.5f;
// Width of the statistics panel, in font sizes
constexpr float StatisticsWidth = 15.0f;
// Cursors A and B are placed at these fractions of the visible range when turned on
constexpr double CursorAPlacement = 1.0 / 3.0;
constexpr double CursorBPlacement = 2.0 / 3.0;
constexpr ImU32 CursorAColor = IM_COL32(255, 199, 77, 255);
constexpr ImU32 CursorBColor = IM_COL32(115, 204, 255, 255);
// The instant of the transient shown on the schematic
constexpr ImU32 PlayheadColor = IM_COL32(255, 96, 96, 255);
// Math channels are drawn with a heavier line than the traces they come from
constexpr float MathLineWeight = 2.0f;
// The plot of logic lanes: its height is a margin for the axis and a row per lane, in font sizes, and at most this
// share of the room
constexpr float LanePlotMargin = 3.5f;
constexpr float LaneRowHeight = 2.2f;
constexpr float MaxLaneShare = 0.6f;
// Bit labels: the room around the digit, in font sizes, and the corner rounding of its box
constexpr float BitBoxPadding = 0.25f;
constexpr float BitBoxRounding = 3.0f;
constexpr const char *ExportLabel = "Export...";
// A new result starts with empty plots until the user picks what to measure
constexpr const char *NothingMeasured =
    "Nothing is measured yet: pick nodes and parts with Probe in the editor, or check traces in the list";
// The harmonics the spectrum measures, and how many the plot shows when it fits: a linear axis spans them, plus one
// harmonic of room, from 0 Hz
constexpr int HighestHarmonic = 10;
constexpr float HarmonicMarkerSize = 3.0f;
constexpr double MinShownDistortionPercent = 0.001;
// The other steps of a stepped run are drawn this opaque, behind the shown step
constexpr float OtherStepAlpha = 0.35f;
// Samples on each side of a step label over which the curves must stay apart
constexpr std::size_t SteadySamples = 2;
// Six decades below the largest density, about where the share of a part stops mattering
constexpr double DecadeFitFloor = 1e-6;
// The first bins above 0 Hz, where the Hann window spreads the mean
constexpr std::ptrdiff_t SpectrumSkirtBins = 2;
// ngspice reports an open path with a resistance around 1e20 ohm, which no readable suffix shows; it is drawn as an
// open circuit instead
constexpr double OpenResistance = 1e15;

/**
 * @struct  PlottedTrace
 * @brief   A trace drawn in a plot, kept so the cursor readout can list its value at any sample.
 */
struct PlottedTrace {
    std::string Label;
    const std::vector<double> *Values;
    const char *Unit;
    ImU32 Color;
    // Set for the lane of a logic signal, whose readout gives its logic state too
    std::optional<double> LogicLevel = std::nullopt;
};

// Axis ticks use the same suffixes as the values the user types, such as "10m" or "1k", followed by the unit.
// ImPlot hands the unit back as untyped user data, which is why callers cast away the const of the literal
int FormatAxisValue(const double value, char *buffer, const int size, void *unit) {
    const std::string text = std::format("{}{}", Core::FormatValue(value), static_cast<const char *>(unit));
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s", text.c_str());
}

// The legend only names the traces: clicking an entry would hide a curve from the plot alone, while the statistics
// and the export still showed it. The trace list is the one place that picks what is shown
bool BeginTracePlot(const char *id, const ImVec2 size) {
    if (!ImPlot::BeginPlot(id, size)) {
        return false;
    }
    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);
    return true;
}

// Splits the room left in the window between the plots
float PlotHeight(const int plot_count) {
    return std::max(ImGui::GetFontSize() * MinPlotHeight,
                    ImGui::GetContentRegionAvail().y / static_cast<float>(plot_count) -
                        ImGui::GetStyle().ItemSpacing.y);
}

// Ground is always 0 V, so plots start at node 1. Each node keeps its color whichever nodes are measured. The
// suffix tells the curves of a DC sweep family apart in the cursor readout
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values, Schematic &schematic,
               const char *unit, const std::string &suffix, std::vector<PlottedTrace> &plotted,
               const std::vector<int> &excluded = {}) {
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

// ImPlot draws no dashes, so the line is drawn by hand: a dummy item gives the legend entry, and an invisible line
// keeps the automatic fit of the axes
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
         SplitIntoDashes(points, DashLength * font_size, DashGap * font_size, plot_min, plot_max)) {
        draw_list->AddLine(dash.Start, dash.End, color, DashWeight);
    }
    ImPlot::PopPlotClipRect();
}

// Currents go on the secondary Y axis, which the caller sets up before plotting anything. Each keeps the color of
// its position in the circuit, as in the editor
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

bool HasMeasuredVoltage(const std::size_t node_count, const Schematic &schematic) {
    for (std::size_t node = 1; node < node_count; ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            return true;
        }
    }
    return false;
}

// While the plot is hovered, marks the sample nearest the cursor and lists every plotted trace at it
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
        std::string text = std::format("{}: {}{}", trace.Label, Core::FormatValue(value), trace.Unit);
        if (trace.LogicLevel) {
            text =
                std::format("{}: {} ({}{})", trace.Label, GetLogicStateName(ReadLogicState(value, *trace.LogicLevel)),
                            Core::FormatValue(value), trace.Unit);
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", text.c_str());
    }
    ImGui::EndTooltip();
}

/**
 * @struct  StepTraces
 * @brief   The X values, node values and currents of one step of a stepped transient or AC sweep, as plotted.
 */
struct StepTraces {
    const std::vector<double> *Xs;
    const std::vector<std::vector<double>> *Nodes;
    const std::vector<Core::ComponentTrace> *Currents;
};

// The other steps of a stepped run, faint behind the shown one and without legend entries, so the family shows its
// spread while the shown step stays readable; currents are thin solid lines on their axis, since dashes would
// clutter a family. Nodes drawn as logic lanes are skipped, as PlotNodes() skips them. Call it before plotting the
// shown step
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
            spec.LineColor.w = OtherStepAlpha;
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
            spec.LineColor.w = OtherStepAlpha;
            ImPlot::PlotLine(std::format("##step{}i{}", step, index).c_str(), traces.Xs->data(), current.Values.data(),
                             count, spec);
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
}

// A curve read at other X values, by straight lines between its samples and held at its ends; both X lists rise, so
// one walk along each does it
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

// The sample where curves of equal length part most and stay apart for a few samples around it, so an edge that
// one step crosses before the others, as in a pulse, does not take the labels
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
        const std::size_t first = index > SteadySamples ? index - SteadySamples : 0;
        const std::size_t last = std::min(index + SteadySamples, count - 1);
        const double steady = *std::min_element(spreads.begin() + static_cast<std::ptrdiff_t>(first),
                                                spreads.begin() + static_cast<std::ptrdiff_t>(last) + 1);
        if (steady >= widest_spread) {
            widest_spread = steady;
            widest = index;
        }
    }
    return widest;
}

// Labels each step of every measured trace with its value, where its curves part most. Every step is read at the X
// values of the shown one, since each transient has its own time points. A trace the steps leave unchanged, such
// as the input, gets no labels, which would only pile up on one line
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
        if ((*highest)[sample] - (*lowest)[sample] <= std::max(largest * NegligibleSpread, ResidueLevel)) {
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

// Picks the step the statistics, the spectrum and the schematic read. Returns the step the user picked, if any
template <typename Result> std::optional<std::size_t> DrawStepSelector(const StepFamily<Result> &steps) {
    std::optional<std::size_t> picked;
    DrawFieldLabel("Shown step");
    const std::string shown = FormatStepValue(steps.Parameter, steps.Values[steps.Selected], steps.Unit);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##shown_step", shown.c_str())) {
        for (std::size_t step = 0; step < steps.Values.size(); ++step) {
            const std::string label = FormatStepValue(steps.Parameter, steps.Values[step], steps.Unit);
            if (ImGui::Selectable(label.c_str(), step == steps.Selected)) {
                picked = step;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The statistics, the spectrum and the schematic read this step; the others are faint");
    return picked;
}

template <typename Result> std::vector<std::string> ListStepLabels(const StepFamily<Result> &steps) {
    std::vector<std::string> labels;
    for (const double value : steps.Values) {
        labels.push_back(FormatStepValue(steps.Parameter, value, steps.Unit));
    }
    return labels;
}

// Labels every curve of each shown trace with the value of the stepped source, so the curves of a family tell
// apart. The labels of a trace go where its curves are furthest apart, which is the end for most traces but the
// peak for a derivative, whose curves all end near zero. Each label sits on the axis of its curve
void LabelCurves(const Core::DCSweep &sweep, const Schematic &schematic, const bool show_currents,
                 const std::vector<std::vector<MathTrace>> &math) {
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    for (const CurveFamily &family : ListCurveFamilies(sweep, schematic, show_currents, math)) {
        const std::size_t sample = FindWidestSpread(family.Curves);
        if (sample >= sweep.SweptValues.size() || !CurvesDiffer(family.Curves, sample)) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, family.Axis);
        // Labels closer than a line of text to one already drawn are skipped, so none is drawn over another
        std::vector<float> drawn;
        for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
            const double y = (*family.Curves[curve])[sample];
            if (!std::isfinite(y)) {
                continue;
            }
            const float pixel_y = ImPlot::PlotToPixels(sweep.SweptValues[sample], y).y;
            if (std::ranges::any_of(
                    drawn, [pixel_y](const float other) { return std::abs(other - pixel_y) < ImGui::GetFontSize(); })) {
                continue;
            }
            drawn.push_back(pixel_y);
            ImPlot::Annotation(sweep.SweptValues[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s",
                               FormatStepLabel(sweep, sweep.Curves[curve]).c_str());
        }
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

// While other traces are shown and no voltage is, the voltage axis keeps its grid but drops its label and
// ticks, so an empty scale is not read as data
ImPlotAxisFlags VoltageAxisFlags(const bool has_voltages, const bool has_other_traces) {
    if (has_voltages || !has_other_traces) {
        return ImPlotAxisFlags_None;
    }
    return ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels;
}

// Measured nodes of a result, ground excluded; empty when the plot shows no voltage
std::vector<int> ListMeasuredNodes(const std::size_t node_count, const Schematic &schematic,
                                   const std::vector<int> &excluded = {}) {
    std::vector<int> nodes;
    for (std::size_t node = 1; node < node_count; ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node)) &&
            !std::ranges::contains(excluded, static_cast<int>(node))) {
            nodes.push_back(static_cast<int>(node));
        }
    }
    return nodes;
}

// The measured nodes of a result that logic parts are connected to, which the transient plots as lanes
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

// Measured currents of a result, in result order; empty when the plot shows no current axis
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

// Fits every axis for a new result; otherwise only the axes whose traces changed, so the zoom of the others stays.
// Call it before each BeginPlot()
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

// Toggles every trace of a section at once; the ID keeps the buttons of each section apart. Returns the new state
// for all of them, or no value when neither button was pressed
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

// Every measured trace of a result, voltages first, with the labels and colors of the plots
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

// Draws the cursors in the current plot, placing them in the visible range the first frame they are shown. On a
// logarithmic axis they are placed by decades, as the axis shows them
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
        cursors.A = place(CursorAPlacement);
        cursors.B = place(CursorBPlacement);
    }
    const ImVec4 color_a = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(CursorAColor));
    const ImVec4 color_b = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(CursorBColor));
    ImPlot::DragLineX(0, &cursors.A, color_a, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.A, color_a, "A");
    ImPlot::DragLineX(1, &cursors.B, color_b, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.B, color_b, "B");
}

// The instant of the transient shown on the schematic, as a line along the plot. Dragging it moves the instant and
// stops the playback, so the line stays where it was dropped
void DrawPlayhead(TransientPlayback &playback, const double stop_time) {
    const ImVec4 color = ImGui::ColorConvertU32ToFloat4(AdaptToBackground(PlayheadColor));
    double time = playback.Time;
    if (ImPlot::DragLineX(2, &time, color, 1.5f, ImPlotDragToolFlags_NoFit)) {
        playback.Time = std::clamp(time, 0.0, stop_time);
        playback.Playing = false;
    }
}

// Turning the cursors on places them again in the current view, so they are never lost off screen
void DrawCursorToggle(PlotCursors &cursors) {
    if (ImGui::Checkbox("Cursors A and B", &cursors.Shown) && cursors.Shown) {
        cursors.Placed = false;
    }
}

// The statistics cover the span between the cursors while they are shown, otherwise what the plot shows
PlotSpan MeasuredSpan(const PlotCursors &cursors, const PlotSpan &view) {
    return cursors.Shown ? PlotSpan{.From = cursors.A, .To = cursors.B} : view;
}

std::string FormatQuantity(const double value, const char *unit) {
    return std::format("{}{}", Core::FormatValue(value), unit);
}

// Decibels and degrees read better with fixed decimals than with SPICE suffixes, which would show 500m dB
std::string FormatFixed(const double value, const char *unit) {
    return Core::FormatFixed(value, 2) + unit;
}

std::string FormatOptionalQuantity(const std::optional<double> value, const char *unit) {
    return value ? FormatQuantity(*value, unit) : "-";
}

// Two-column rows of a statistics table: what is measured and its value
void DrawStatistic(const char *name, const std::string &value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
}

// A negligible value reads as ~0, so it is not mistaken for an exact zero; hovering it shows the exact value
void DrawMeasuredValue(const char *name, const double value, const double scale, const char *unit) {
    if (!IsNegligible(value, scale)) {
        DrawStatistic(name, FormatQuantity(value, unit));
        return;
    }
    DrawStatistic(name, std::format("~0{}", unit));
    ImGui::SetItemTooltip("%s",
                          std::format("{}{}, negligible next to the trace", Core::FormatValue(value), unit).c_str());
}

bool BeginStatisticsTable(const std::string &id) {
    return ImGui::BeginTable(id.c_str(), 2, ImGuiTableFlags_SizingStretchProp);
}

// Splits a tab into the plots and the statistics panel on their right
template <typename DrawPlots, typename DrawStatistics>
void DrawWithStatistics(const DrawPlots &draw_plots, const DrawStatistics &draw_statistics) {
    const float statistics_width = ImGui::GetFontSize() * StatisticsWidth;
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

// The current axis may already be set up for probed currents; the third axis is labeled with its unit, which
// must outlive the plot because ImPlot formats the ticks when it ends
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

// Fits the axes of the math traces when the channels change, like the probed traces
void FitMathAxes(const std::vector<MathTrace> &math) {
    for (const ImAxis axis : {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3}) {
        if (UsesAxis(math, axis)) {
            ImPlot::SetNextAxisToFit(axis);
        }
    }
}

// Division by zero leaves NaN samples, which are skipped instead of breaking the line
void PlotMath(const std::vector<double> &xs, const std::vector<MathTrace> &math, const std::string &suffix,
              std::vector<PlottedTrace> &plotted) {
    for (const MathTrace &trace : math) {
        ImPlot::SetAxes(ImAxis_X1, trace.Axis);
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
        spec.LineWeight = MathLineWeight;
        spec.Flags = ImPlotLineFlags_SkipNaN;
        ImPlot::PlotLine(trace.Label.c_str(), xs.data(), trace.Values.data(), static_cast<int>(xs.size()), spec);
        plotted.push_back({trace.Label + suffix, &trace.Values, trace.Unit.c_str(), trace.Color});
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

void DrawMathErrors(const std::vector<std::string> &errors) {
    for (const std::string &error : errors) {
        ImGui::TextColored(GetWarningTextColor(), "%s", error.c_str());
    }
}

// Like ImGui::BeginTabBar(), but the bar and the line under it stop short of the right edge by the given width,
// leaving room for a button there. Mirrors BeginTabBar() in imgui_widgets.cpp, which takes no width
bool BeginTabBarBefore(const char *id, const float reserved_width) {
    ImGuiContext &context = *ImGui::GetCurrentContext();
    ImGuiWindow *window = context.CurrentWindow;
    if (window->SkipItems) {
        return false;
    }
    const ImGuiID tab_bar_id = window->GetID(id);
    ImGuiTabBar *tab_bar = context.TabBars.GetOrAddByKey(tab_bar_id);
    const ImVec2 start = window->DC.CursorPos;
    const ImRect bounds(start.x, start.y, window->WorkRect.Max.x - reserved_width,
                        start.y + context.FontSize + context.Style.FramePadding.y * 2.0f);
    tab_bar->ID = tab_bar_id;
    tab_bar->SeparatorMinX = bounds.Min.x - IM_TRUNC(window->WindowPadding.x * 0.5f);
    tab_bar->SeparatorMaxX = bounds.Max.x;
    return ImGui::BeginTabBarEx(tab_bar, bounds, ImGuiTabBarFlags_IsFocused);
}

const char *GetTabFileSuffix(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OperatingPoint:
        return "operating-point";
    case PlotTab::Transient:
        return "transient";
    case PlotTab::Spectrum:
        return "spectrum";
    case PlotTab::ACSweep:
        return "ac-sweep";
    case PlotTab::DCSweep:
        return "dc-sweep";
    case PlotTab::Noise:
        return "noise";
    case PlotTab::TransferFunction:
        return "transfer-function";
    }
    return "plot";
}

const char *GetTabTitle(const PlotTab tab) {
    switch (tab) {
    case PlotTab::OperatingPoint:
        return "operating point";
    case PlotTab::Transient:
        return "transient";
    case PlotTab::Spectrum:
        return "spectrum";
    case PlotTab::ACSweep:
        return "AC sweep";
    case PlotTab::DCSweep:
        return "DC sweep";
    case PlotTab::Noise:
        return "noise";
    case PlotTab::TransferFunction:
        return "transfer function";
    }
    return "";
}

// The operating point is a table of DC values, one row per node and per current
void DrawOperatingPoint(const Core::OperatingPoint &operating_point, Schematic &schematic) {
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : operating_point.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
    constexpr ImGuiTableFlags TableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    // Fixed widths, so the currents fit beside the voltages
    const ImVec2 table_size = {ImGui::GetFontSize() * 14.0f, 0.0f};
    if (ImGui::BeginTable("node_voltages", 2, TableFlags, table_size)) {
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Voltage");
        ImGui::TableHeadersRow();
        for (std::size_t node = 0; node < operating_point.NodeVoltages.size(); ++node) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // A node a net label named shows its name before its number
            const std::optional<std::string> net = schematic.GetConnectivity().GetNetName(static_cast<int>(node));
            std::string name = std::format("{}", node);
            if (node == 0) {
                name = "0 (ground)";
            } else if (net) {
                name = std::format("{} ({})", *net, node);
            }
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}V", Core::FormatValue(operating_point.NodeVoltages[node])).c_str());
        }
        ImGui::EndTable();
    }
    if (operating_point.Currents.empty()) {
        return;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
    if (ImGui::BeginTable("currents", 2, TableFlags, table_size)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("Current");
        ImGui::TableHeadersRow();
        for (const Core::ComponentCurrent &current : operating_point.Currents) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(current.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}A", Core::FormatValue(current.Current)).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Currents as in SPICE: positive through a two-terminal part from its first terminal to its "
                        "second, and into each terminal of a transistor");
    ImGui::PopTextWrapPos();
}

// The transfer function is a table of three small-signal DC values; an impedance that ngspice reports as practically
// infinite, such as an input path held open, reads as an open circuit instead of a giant number
void DrawTransferFunction(const Core::TransferFunction &transfer, Schematic &schematic) {
    constexpr ImGuiTableFlags TableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("transfer_function", 3, TableFlags)) {
        ImGui::TableSetupColumn("Quantity");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Unit");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Gain");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Core::FormatValue(transfer.Gain).c_str());
        ImGui::TableNextColumn();
        // The gain is a ratio of like units for a voltage source and a transimpedance for a current source
        ImGui::TextUnformatted(std::format("V/{}", transfer.InputUnit).c_str());
        const auto draw_impedance = [](const char *name, const double ohms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", name);
            ImGui::TableNextColumn();
            const bool open = !std::isfinite(ohms) || std::abs(ohms) >= OpenResistance;
            ImGui::TextUnformatted(open ? "open" : Core::FormatValue(ohms).c_str());
            ImGui::TableNextColumn();
            if (!open) {
                ImGui::TextUnformatted("Ohm");
            }
        };
        draw_impedance("Input impedance", transfer.InputImpedance);
        draw_impedance("Output impedance", transfer.OutputImpedance);
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Small-signal DC gain from %s to %s", transfer.InputSource.c_str(),
                        GetVoltageLabel(schematic.GetConnectivity(), transfer.OutputNode).c_str());
    ImGui::PopTextWrapPos();
}

// The range a logarithmic axis fits to: the positive values of the traces, with a tenth of their decades, and at
// least half a decade, of room above and below. The first trace is always in full; values of the others far below
// its peak, such as the share of the ideal op-amp, are left below the axis, so they do not squash the plot
PlotSpan GetDecadeFitRange(const std::vector<std::span<const double>> &traces) {
    double highest = 0.0;
    double reference = 0.0;
    for (std::size_t index = 0; index < traces.size(); ++index) {
        for (const double value : traces[index]) {
            highest = std::max(highest, value);
            if (index == 0) {
                reference = std::max(reference, value);
            }
        }
    }
    if (highest <= 0.0) {
        return {.From = 1e-12, .To = 1.0};
    }
    double lowest = highest;
    for (std::size_t index = 0; index < traces.size(); ++index) {
        for (const double value : traces[index]) {
            if (value > 0.0 && (index == 0 || value > reference * DecadeFitFloor)) {
                lowest = std::min(lowest, value);
            }
        }
    }
    const double decades = std::log10(highest / lowest);
    const double padding = std::max(decades * 0.1, 0.5);
    return {.From = lowest / std::pow(10.0, padding), .To = highest * std::pow(10.0, padding)};
}

std::string FormatFrequencyRange(const Core::Noise &noise) {
    return std::format("{}Hz to {}Hz", Core::FormatValue(noise.Frequencies.front()),
                       Core::FormatValue(noise.Frequencies.back()));
}

// The output in the color of its node, the shares checked in the panel, and the input-referred noise dashed
void PlotNoiseTraces(const Core::Noise &noise, const Connectivity &connectivity, const std::set<std::string> &shown,
                     const bool input_shown, const std::string &output_unit, const std::string &input_unit,
                     std::vector<PlottedTrace> &plotted) {
    const std::string output_label = GetVoltageLabel(connectivity, noise.OutputNode);
    const ImU32 output_color = GetNodeColor(noise.OutputNode);
    ImPlotSpec output_spec;
    output_spec.LineColor = ImGui::ColorConvertU32ToFloat4(output_color);
    output_spec.LineWeight = MathLineWeight;
    ImPlot::PlotLine(output_label.c_str(), noise.Frequencies.data(), noise.OutputDensities.data(),
                     static_cast<int>(noise.Frequencies.size()), output_spec);
    plotted.push_back({output_label, &noise.OutputDensities, output_unit.c_str(), output_color});
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (!shown.contains(contribution.Name)) {
            continue;
        }
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
        ImPlot::PlotLine(contribution.Name.c_str(), noise.Frequencies.data(), contribution.OutputDensities.data(),
                         static_cast<int>(noise.Frequencies.size()), spec);
        plotted.push_back(
            {contribution.Name, &contribution.OutputDensities, output_unit.c_str(), GetCurrentColor(index)});
    }
    if (!input_shown) {
        return;
    }
    const std::string input_label = std::format("Input-referred ({})", noise.InputSource);
    const ImU32 input_color = AdaptToBackground(InputNoiseColor);
    ImPlot::SetAxes(ImAxis_X1, noise.InputUnit == "V" ? ImAxis_Y1 : ImAxis_Y2);
    PlotDashedLine(input_label, noise.Frequencies, noise.InputDensities, input_color);
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    plotted.push_back({input_label, &noise.InputDensities, input_unit.c_str(), input_color});
}

// Decibels relative to 1 of the unit of the trace, as spectrum analyzers write dBV
std::string GetDecibelUnit(const SpectrumTrace &trace) {
    return trace.Unit.empty() ? " dB" : std::format(" dB{}", trace.Unit);
}

// Distortion reads as a percentage with three significant digits, down to the floor the interpolation of the
// transient leaves; the decibels give the exact figure below it
std::string FormatDistortionPercent(const double ratio) {
    const double percent = ratio * 100.0;
    return percent < MinShownDistortionPercent ? std::format("<{}%", MinShownDistortionPercent)
                                               : std::format("{:.3g}%", percent);
}

// Every trace of a spectrum shares the span and the window, so one warning covers them all when the span holds too
// few periods to tell the harmonics apart
void DrawPeriodsWarning(const TransientSpectra &spectra) {
    const auto short_trace = std::ranges::find_if(
        spectra.Traces, [](const SpectrumTrace &trace) { return trace.Harmonics.PeriodsNeeded.has_value(); });
    if (short_trace == spectra.Traces.end()) {
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(GetWarningTextColor(), "%s",
                       std::format("The stretch needs {} periods to tell the harmonics apart: widen it, or pick the "
                                   "Rectangular window over whole periods",
                                   *short_trace->Harmonics.PeriodsNeeded)
                           .c_str());
    ImGui::PopTextWrapPos();
}

// The rows of one trace in its statistics table: its mean, its fundamental and THD, then each harmonic in dBc
void DrawHarmonics(const SpectrumTrace &trace) {
    const HarmonicAnalysis &analysis = trace.Harmonics;
    const char *unit = trace.Unit.c_str();
    DrawMeasuredValue("Mean", analysis.Mean, trace.LargestAmplitude, unit);
    if (analysis.Harmonics.empty()) {
        DrawStatistic("Freq", "-");
        return;
    }
    const Harmonic &fundamental = analysis.Harmonics.front();
    DrawStatistic("Freq", FormatQuantity(fundamental.Frequency, "Hz"));
    ImGui::SetItemTooltip("Frequency of the fundamental, the strongest tone");
    DrawStatistic("Amplitude", FormatQuantity(fundamental.Amplitude, unit));
    ImGui::SetItemTooltip("Peak amplitude of the fundamental");
    if (analysis.TotalDistortion) {
        DrawStatistic("THD", FormatDistortionPercent(*analysis.TotalDistortion));
        DrawStatistic("THD dB", FormatFixed(20.0 * std::log10(std::max(*analysis.TotalDistortion, 1e-15)), " dB"));
    } else {
        DrawStatistic("THD", "-");
    }
    for (std::size_t index = 1; index < analysis.Harmonics.size(); ++index) {
        const Harmonic &harmonic = analysis.Harmonics[index];
        const std::string name = std::format("H{}", harmonic.Order);
        const double relative = harmonic.Amplitude / fundamental.Amplitude;
        DrawStatistic(name.c_str(), FormatFixed(20.0 * std::log10(std::max(relative, 1e-15)), " dBc"));
        ImGui::SetItemTooltip("%s", std::format("{}Hz, {}{}", Core::FormatValue(harmonic.Frequency),
                                                Core::FormatValue(harmonic.Amplitude), unit)
                                        .c_str());
    }
}

// The frequency range a new spectrum opens on: the fundamental and its harmonics on a linear axis, so they spread
// across the plot, and every frequency on a logarithmic one
PlotSpan GetSpectrumFitRange(const TransientSpectra &spectra, const bool logarithmic) {
    const PlotSpan whole{.From = spectra.Frequencies.front(), .To = spectra.Frequencies.back()};
    if (logarithmic) {
        return whole;
    }
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (!trace.Harmonics.Harmonics.empty()) {
            const double fundamental = trace.Harmonics.Harmonics.front().Frequency;
            return {.From = 0.0, .To = std::min(fundamental * (HighestHarmonic + 1), whole.To)};
        }
    }
    // Without a measured fundamental, as in a stretch of too few periods, the strongest bin past the skirt of the
    // mean stands in for it
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (trace.Decibels && trace.Decibels->size() > static_cast<std::size_t>(SpectrumSkirtBins)) {
            const auto strongest = std::max_element(trace.Decibels->begin() + SpectrumSkirtBins, trace.Decibels->end());
            const double frequency = spectra.Frequencies[static_cast<std::size_t>(strongest - trace.Decibels->begin())];
            return {.From = 0.0, .To = std::min(frequency * (HighestHarmonic + 1), whole.To)};
        }
    }
    return {.From = 0.0, .To = whole.To};
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose results are plotted; it must outlive the window.
 * @param[in] playback   Instant of the transient shown on the schematic, drawn as a line in the transient plot that
 *                       can be dragged; it must outlive the window.
 */
OutputWindow::OutputWindow(Schematic &schematic, TransientPlayback &playback)
    : AppWindow("Output", true), m_Schematic(schematic), m_Playback(playback) {
    SetInitialSize(InitialSize);
    // Nothing to plot yet; the first result opens it
    SetOpen(false);
}

/**
 * @brief   Opens the window, if closed, on one of its tabs, and brings it to the front on the next frame.
 * @param[in] tab  Tab of the analysis whose result just arrived.
 * @note    When the user turned off bringing it to the front, the window marks its title or tab with a dot
 *          instead, until it is shown.
 */
void OutputWindow::ShowTab(const PlotTab tab) {
    m_TabToShow = tab;
    if (m_FrontOnNewResult) {
        SetOpen(true);
        Focus();
    } else {
        OpenBehind();
        SetNewContentMarker(true);
    }
}

/**
 * @brief   Draws the View menu items of the window, inside the main menu bar.
 */
void OutputWindow::DrawViewMenuItems() {
    if (ImGui::MenuItem("Bring Output to Front on Results", nullptr, &m_FrontOnNewResult)) {
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SetItemTooltip("Otherwise a new result leaves the current tab in front and marks Output with a dot");
}

/**
 * @brief   Keeps the View preference of the window in imgui.ini, next to the window layout, so it survives a
 *          restart.
 */
void OutputWindow::RegisterSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = SettingsTypeName;
    handler.TypeHash = ImHashStr(SettingsTypeName);
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, const char *) -> void * {
        return settings->UserData;
    };
    handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
        constexpr std::string_view FrontKey = "FrontOnNewResult=";
        const std::string_view text(line);
        if (text.starts_with(FrontKey)) {
            static_cast<OutputWindow *>(entry)->m_FrontOnNewResult = text.substr(FrontKey.size()) == "1";
        }
    };
    handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *settings, ImGuiTextBuffer *buffer) {
        const auto *output = static_cast<const OutputWindow *>(settings->UserData);
        buffer->appendf("[%s][Preferences]\n", SettingsTypeName);
        buffer->appendf("FrontOnNewResult=%d\n\n", output->m_FrontOnNewResult ? 1 : 0);
    };
    ImGui::AddSettingsHandler(&handler);
}

void OutputWindow::Draw() {
    // Drawn means shown, so the new result is seen; a window that just appeared behind another tab may be drawn
    // once before it is hidden
    if (!ImGui::IsWindowAppearing()) {
        SetNewContentMarker(false);
    }
    m_Exporter.Update();
    m_Exporter.DrawError();
    const std::size_t node_count = GetListedNodeCount();
    const std::vector<Core::ComponentTrace> &currents = GetListedCurrents();
    if (node_count > 1 || !currents.empty()) {
        DrawTraceList(node_count, currents);
        ImGui::SameLine();
    }

    if (!ImGui::BeginChild("plots")) {
        ImGui::EndChild();
        return;
    }
    // The Export button goes on the right of the tab row, drawn once the tabs are done
    const ImVec2 tab_row = ImGui::GetCursorScreenPos();
    const float tab_row_width = ImGui::GetContentRegionAvail().x;
    const float export_width = ImGui::CalcTextSize(ExportLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (!BeginTabBarBefore("plots", export_width + ImGui::GetStyle().ItemSpacing.x)) {
        ImGui::EndChild();
        return;
    }
    const FrameResults frame = EvaluateFrame(node_count, currents);
    // Applies to the automatic fit of every new result and to the user's double-click fit
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, FitPadding);
    DrawTabs(frame);
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
    DrawExportButton(tab_row, tab_row_width, export_width, frame);
    ImGui::EndChild();
}

// Every result comes from the same circuit, so they have the same components; the first result there is lists them
const std::vector<Core::ComponentTrace> &OutputWindow::GetListedCurrents() const {
    static const std::vector<Core::ComponentTrace> no_currents;
    if (const auto &transient = m_Schematic.GetTransient()) {
        return transient->Currents;
    }
    if (const auto &sweep = m_Schematic.GetACSweep()) {
        return sweep->CurrentMagnitudesDecibels;
    }
    if (const auto &dc_sweep = m_Schematic.GetDCSweep()) {
        return dc_sweep->Curves.front().Currents;
    }
    return no_currents;
}

// The nodes of the results, which share them as they share the components
std::size_t OutputWindow::GetListedNodeCount() const {
    const auto &transient = m_Schematic.GetTransient();
    const auto &sweep = m_Schematic.GetACSweep();
    const auto &dc_sweep = m_Schematic.GetDCSweep();
    return std::max({transient ? transient->NodeVoltages.size() : 0, sweep ? sweep->NodeMagnitudesDecibels.size() : 0,
                     dc_sweep ? dc_sweep->Curves.front().NodeVoltages.size() : 0});
}

// Math channels are evaluated once per frame and result, for both the plot and its statistics. Every curve of a DC
// sweep has the same nodes and currents, so the first one reports the errors of all
OutputWindow::FrameResults OutputWindow::EvaluateFrame(const std::size_t node_count,
                                                       const std::vector<Core::ComponentTrace> &currents) const {
    FrameResults frame;
    if (const auto &transient = m_Schematic.GetTransient()) {
        frame.TransientMath = m_MathChannels.Evaluate(transient->Times, {.Seconds = 1}, transient->NodeVoltages,
                                                      transient->Currents, frame.TransientErrors);
    }
    if (const auto &dc_sweep = m_Schematic.GetDCSweep()) {
        const Dimension swept_unit = dc_sweep->SweptUnit == "A" ? Dimension{.Amperes = 1} : Dimension{.Volts = 1};
        for (const Core::DCSweepCurve &curve : dc_sweep->Curves) {
            std::vector<std::string> curve_errors;
            frame.DCSweepMath.push_back(m_MathChannels.Evaluate(dc_sweep->SweptValues, swept_unit, curve.NodeVoltages,
                                                                curve.Currents, curve_errors));
            if (frame.DCSweepMath.size() == 1) {
                frame.DCSweepErrors = std::move(curve_errors);
            }
        }
    }
    frame.AnythingMeasured = HasMeasuredVoltage(node_count, m_Schematic) || HasMeasuredCurrent(currents, m_Schematic);
    return frame;
}

// The check marks take the colors of the curves, which match the probes in the editor
void OutputWindow::DrawTraceList(const std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) {
    ImGui::BeginChild("traces", ImVec2(ImGui::GetFontSize() * TraceListWidth, 0.0f), ImGuiChildFlags_Borders);
    if (node_count > 1) {
        ImGui::TextDisabled("Voltages");
        const std::optional<bool> measure_all = DrawAllNoneButtons("voltages");
        for (int node = 1; node < static_cast<int>(node_count); ++node) {
            if (measure_all) {
                m_Schematic.SetVoltageMeasured(node, *measure_all);
            }
            bool measured = m_Schematic.IsVoltageMeasured(node);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetNodeColor(node));
            if (ImGui::Checkbox(GetVoltageLabel(m_Schematic.GetConnectivity(), node).c_str(), &measured)) {
                m_Schematic.SetVoltageMeasured(node, measured);
            }
            ImGui::PopStyleColor();
        }
    }

    if (!currents.empty()) {
        ImGui::TextDisabled("Currents");
        const std::optional<bool> measure_all = DrawAllNoneButtons("currents");
        for (std::size_t index = 0; index < currents.size(); ++index) {
            const std::string &name = currents[index].Name;
            if (measure_all) {
                m_Schematic.SetCurrentMeasured(name, *measure_all);
            }
            bool measured = m_Schematic.IsCurrentMeasured(name);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(index));
            if (ImGui::Checkbox(std::format("I({})", name).c_str(), &measured)) {
                m_Schematic.SetCurrentMeasured(name, measured);
            }
            ImGui::PopStyleColor();
        }
    }

    std::vector<std::string> operands;
    for (std::size_t node = 1; node < node_count; ++node) {
        operands.push_back(std::format("V({})", node));
    }
    for (const Core::ComponentTrace &current : currents) {
        operands.push_back(std::format("I({})", current.Name));
    }
    m_MathChannels.Draw(operands);
    ImGui::EndChild();
}

// One tab per analysis, and the spectrum of the transient; a new result brings its tab to the front once
void OutputWindow::DrawTabs(const FrameResults &frame) {
    const std::optional<PlotTab> tab_to_show = std::exchange(m_TabToShow, std::nullopt);
    const auto begin_tab = [this, &tab_to_show](const char *label, const PlotTab tab) {
        if (!ImGui::BeginTabItem(label, nullptr,
                                 tab_to_show == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            return false;
        }
        m_ShownTab = tab;
        return true;
    };
    if (begin_tab("Operating point", PlotTab::OperatingPoint)) {
        if (const auto &operating_point = m_Schematic.GetOperatingPoint()) {
            DrawOperatingPoint(*operating_point, m_Schematic);
        } else {
            ImGui::TextDisabled("No operating point for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    if (begin_tab("Transient", PlotTab::Transient)) {
        DrawTransientTab(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("Spectrum", PlotTab::Spectrum)) {
        DrawSpectrumTab(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("AC sweep", PlotTab::ACSweep)) {
        DrawACSweepTab(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("DC sweep", PlotTab::DCSweep)) {
        DrawDCSweepTab(frame);
        ImGui::EndTabItem();
    }
    if (begin_tab("Noise", PlotTab::Noise)) {
        DrawNoiseTab();
        ImGui::EndTabItem();
    }
    if (begin_tab("Transfer function", PlotTab::TransferFunction)) {
        DrawTransferFunctionTab();
        ImGui::EndTabItem();
    }
}

void OutputWindow::DrawTransientTab(const FrameResults &frame) {
    const auto &transient = m_Schematic.GetTransient();
    if (!transient) {
        ImGui::TextDisabled("No transient for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            if (const auto &steps = m_Schematic.GetTransientSteps()) {
                if (const std::optional<std::size_t> picked = DrawStepSelector(*steps)) {
                    m_Schematic.SelectTransientStep(*picked);
                }
            }
            if (!frame.AnythingMeasured && frame.TransientMath.empty()) {
                ImGui::TextDisabled("%s", NothingMeasured);
            }
            DrawMathErrors(frame.TransientErrors);
            DrawTransient(*transient, frame.TransientMath);
        },
        [&] { DrawTransientStatistics(*transient, frame.TransientMath); });
}

void OutputWindow::DrawSpectrumTab(const FrameResults &frame) {
    const auto &transient = m_Schematic.GetTransient();
    if (!transient) {
        ImGui::TextDisabled("No transient for the current circuit; the spectrum transforms its measured traces");
        return;
    }
    UpdateSpectra(*transient, frame.TransientMath);
    DrawWithStatistics(
        [&] {
            if (!frame.AnythingMeasured && frame.TransientMath.empty()) {
                ImGui::TextDisabled("%s", NothingMeasured);
            }
            DrawMathErrors(frame.TransientErrors);
            DrawSpectrum();
        },
        [&] { DrawSpectrumStatistics(); });
}

void OutputWindow::DrawACSweepTab(const FrameResults &frame) {
    const auto &sweep = m_Schematic.GetACSweep();
    if (!sweep) {
        ImGui::TextDisabled("No AC sweep for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            ImGui::TextDisabled("%s",
                                frame.AnythingMeasured
                                    ? "Relative to the AC magnitude of the sources: a magnitude of 1 reads as gain"
                                    : NothingMeasured);
            if (!m_MathChannels.IsEmpty()) {
                ImGui::TextDisabled("Math channels are not calculated for AC sweeps yet");
            }
            if (const auto &steps = m_Schematic.GetACSweepSteps()) {
                if (const std::optional<std::size_t> picked = DrawStepSelector(*steps)) {
                    m_Schematic.SelectACSweepStep(*picked);
                }
            }
            DrawACSweep(*sweep);
        },
        [&] { DrawACSweepStatistics(*sweep); });
}

void OutputWindow::DrawDCSweepTab(const FrameResults &frame) {
    const auto &dc_sweep = m_Schematic.GetDCSweep();
    if (!dc_sweep) {
        ImGui::TextDisabled("No DC sweep for the current circuit; pick it beside Run in the editor");
        return;
    }
    if (!frame.AnythingMeasured && (frame.DCSweepMath.empty() || frame.DCSweepMath.front().empty())) {
        ImGui::TextDisabled("%s", NothingMeasured);
    }
    DrawMathErrors(frame.DCSweepErrors);
    DrawDCSweep(*dc_sweep, frame.DCSweepMath);
}

// An output held by an ideal source, or cut off from every noisy part, has nothing to plot, so the tab says why
void OutputWindow::DrawNoiseTab() {
    const auto &noise = m_Schematic.GetNoise();
    if (!noise) {
        ImGui::TextDisabled("No noise analysis for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawWithStatistics(
        [&] {
            if (noise->OutputTotal > 0.0) {
                DrawNoise(*noise);
                return;
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(GetWarningTextColor(), "%s",
                               std::format("No noise reaches {}: an ideal source holds it, or no part that adds "
                                           "noise drives it. Pick another output beside Run",
                                           GetVoltageLabel(m_Schematic.GetConnectivity(), noise->OutputNode))
                                   .c_str());
            ImGui::PopTextWrapPos();
        },
        [&] { DrawNoiseStatistics(*noise); });
}

// The transfer function is a table, not a plot, so the tab draws it alone
void OutputWindow::DrawTransferFunctionTab() {
    const auto &transfer = m_Schematic.GetTransferFunction();
    if (!transfer) {
        ImGui::TextDisabled("No transfer function for the current circuit; pick it beside Run in the editor");
        return;
    }
    DrawTransferFunction(*transfer, m_Schematic);
}

// The signals of logic parts are drawn as lanes in a plot under the analog traces, which share its time axis. The
// analog plot is left out when it would be empty, so a circuit measured only at logic nodes shows the lanes alone
void OutputWindow::DrawTransient(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    const std::vector<LogicNode> lanes = FindMeasuredLanes(transient.NodeVoltages.size());
    const std::vector<int> lane_nodes = ListNodes(lanes);
    std::vector<int> analog_nodes = ListMeasuredNodes(transient.NodeVoltages.size(), m_Schematic, lane_nodes);
    const bool has_voltages = !analog_nodes.empty() || UsesAxis(math, ImAxis_Y1);
    std::vector<std::string> measured_currents = ListMeasuredCurrents(transient.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool has_analog_plot = has_voltages || show_currents || !math.empty() || lanes.empty();
    const bool currents_changed = m_TransientFit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed = m_TransientFit.TakeNodesChanged(std::move(analog_nodes));
    const bool new_result = m_TransientFit.TakeNewResult(m_Schematic.GetTransientVersion());
    SetNextFits(new_result, voltages_changed, currents_changed);
    if (m_TransientFit.TakeMathChanged(ListMathLabels(math))) {
        FitMathAxes(math);
    }
    float analog_height = PlotHeight(1);
    float lane_height = analog_height;
    if (!lanes.empty() && has_analog_plot) {
        const float font_size = ImGui::GetFontSize();
        const float available = ImGui::GetContentRegionAvail().y;
        lane_height = std::clamp(font_size * (LanePlotMargin + static_cast<float>(lanes.size()) * LaneRowHeight),
                                 font_size * MinPlotHeight, available * MaxLaneShare);
        analog_height =
            std::max(font_size * MinPlotHeight, available - lane_height - ImGui::GetStyle().ItemSpacing.y * 2.0f);
    }
    if (has_analog_plot) {
        DrawTransientAnalog(transient, math, lane_nodes, show_currents, has_voltages, analog_height);
    }
    if (!lanes.empty()) {
        if (new_result) {
            ImPlot::SetNextAxisToFit(ImAxis_X1);
        }
        DrawLogicLanes(transient, lanes, lane_height);
    }
}

void OutputWindow::DrawTransientAnalog(const Core::Transient &transient, const std::vector<MathTrace> &math,
                                       const std::vector<int> &lane_nodes, const bool show_currents,
                                       const bool has_voltages, const float height) {
    if (!BeginTracePlot("##transient", ImVec2(-1.0f, height))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents || !math.empty()));
    if (!lane_nodes.empty()) {
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_TransientView.From, &m_TransientView.To);
    }
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    const std::string third_unit = ThirdAxisUnit(math);
    SetupMathAxes(math, show_currents, third_unit);
    const auto &steps = m_Schematic.GetTransientSteps();
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
        LabelSteps(step_traces, steps->Selected, ListStepLabels(*steps), m_Schematic, show_currents, lane_nodes);
    }
    DrawCursors(m_TransientCursors, false);
    DrawPlayhead(m_Playback, transient.Times.back());
    DrawCursorReadout(transient.Times, "s", false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_TransientView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// The logic signals the plot shows as lanes: the measured nodes of logic parts, unless lanes are turned off
std::vector<LogicNode> OutputWindow::FindMeasuredLanes(const std::size_t node_count) const {
    if (!m_ShowLogicLanes) {
        return {};
    }
    return ListMeasuredLogicNodes(FindLogicNodes(m_Schematic.BuildCircuit()), node_count, m_Schematic);
}

// Writes 0, 1 or X in a box at the middle of every cell of a lane that has room for it, within what the plot shows,
// so zooming out drops the labels that no longer fit
void DrawBitValues(const std::vector<LogicSegment> &cells, const std::size_t lane_position, const ImU32 color) {
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    const double middle = static_cast<double>(lane_position) * LaneStride + LaneHighOffset / 2.0;
    const float pixel_y = ImPlot::PlotToPixels(view.Min, middle).y;
    const float padding = ImGui::GetFontSize() * BitBoxPadding;
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
        draw_list->AddRectFilled(box_min, box_min + box_size, ImGui::GetColorU32(ImGuiCol_FrameBg), BitBoxRounding);
        draw_list->AddRect(box_min, box_min + box_size, color, BitBoxRounding);
        draw_list->AddText(box_min + ImVec2(padding, padding), ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    ImPlot::PopPlotClipRect();
}

// Each lane is a trace between its low and high levels, named on the axis, with a state read at the cursor. A lane
// the circuit drives to a voltage between the thresholds is drawn halfway between low and high
void OutputWindow::DrawLogicLanes(const Core::Transient &transient, const std::vector<LogicNode> &lanes,
                                  const float height) {
    if (!ImPlot::BeginPlot("##transient_logic", ImVec2(-1.0f, height),
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        return;
    }
    const auto top = static_cast<double>(lanes.size() - 1) * LaneStride + LaneHighOffset + LaneMargin;
    ImPlot::SetupAxes("Time", "", ImPlotAxisFlags_None,
                      ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks);
    ImPlot::SetupAxisLimits(ImAxis_Y1, LaneLowOffset - LaneMargin, top, ImPlotCond_Always);
    ImPlot::SetupAxisLinks(ImAxis_X1, &m_TransientView.From, &m_TransientView.To);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    // The first lane is on top, and each is named at its middle
    std::vector<double> positions;
    std::vector<std::string> names;
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const std::size_t height_index = lanes.size() - 1 - lane;
        positions.push_back(static_cast<double>(height_index) * LaneStride + LaneHighOffset / 2.0);
        names.push_back(GetVoltageLabel(m_Schematic.GetConnectivity(), lanes[lane].Node));
    }
    std::vector<const char *> labels;
    for (const std::string &name : names) {
        labels.push_back(name.c_str());
    }
    ImPlot::SetupAxisTicks(ImAxis_Y1, positions.data(), static_cast<int>(positions.size()), labels.data());
    std::vector<PlottedTrace> plotted;
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const LogicNode &node = lanes[lane];
        const std::vector<double> &voltages = transient.NodeVoltages[static_cast<std::size_t>(node.Node)];
        const std::vector<double> heights = BuildLane(voltages, node.LogicLevel, lanes.size() - 1 - lane);
        const ImU32 color = GetNodeColor(node.Node);
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        ImPlot::PlotLine(names[lane].c_str(), transient.Times.data(), heights.data(), static_cast<int>(heights.size()),
                         spec);
        plotted.push_back({names[lane], &voltages, "V", color, node.LogicLevel});
    }
    if (m_ShowBitValues) {
        std::vector<LogicSignal> signals;
        for (const LogicNode &node : lanes) {
            signals.push_back({transient.NodeVoltages[static_cast<std::size_t>(node.Node)], node.LogicLevel});
        }
        const ImPlotRange view = ImPlot::GetPlotLimits().X;
        const auto cells = FindBitCells(transient.Times, signals, view.Min, view.Max);
        for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
            DrawBitValues(cells[lane], lanes.size() - 1 - lane, GetNodeColor(lanes[lane].Node));
        }
    }
    DrawCursors(m_TransientCursors, false);
    DrawPlayhead(m_Playback, transient.Times.back());
    DrawCursorReadout(transient.Times, "s", false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_TransientView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
// their phases follow so each current stays on the same side in both plots. Both plots share their frequency
// range and cursors, so zooming one zooms the other
void OutputWindow::DrawACSweep(const Core::ACSweep &sweep) {
    const bool fit = m_ACSweepFit.TakeNewResult(m_Schematic.GetACSweepVersion());
    const auto &steps = m_Schematic.GetACSweepSteps();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(sweep.CurrentMagnitudesDecibels, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = m_ACSweepFit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed =
        m_ACSweepFit.TakeNodesChanged(ListMeasuredNodes(sweep.NodeMagnitudesDecibels.size(), m_Schematic));
    const ImPlotAxisFlags voltage_axis_flags =
        VoltageAxisFlags(HasMeasuredVoltage(sweep.NodeMagnitudesDecibels.size(), m_Schematic), show_currents);
    const float height = PlotHeight(2);
    SetNextFits(fit, voltages_changed, currents_changed);
    if (BeginTracePlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Voltage (dB)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_ACSweepView.From, &m_ACSweepView.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current (dB)", ImPlotAxisFlags_AuxDefault);
        }
        std::vector<StepTraces> step_traces;
        if (steps) {
            for (const Core::ACSweep &step : steps->Results) {
                step_traces.push_back(
                    {&step.Frequencies, &step.NodeMagnitudesDecibels, &step.CurrentMagnitudesDecibels});
            }
            PlotOtherSteps(step_traces, steps->Selected, m_Schematic, show_currents, {});
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        }
        if (steps) {
            LabelSteps(step_traces, steps->Selected, ListStepLabels(*steps), m_Schematic, show_currents, {});
        }
        DrawCursors(m_ACSweepCursors, true);
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
    SetNextFits(fit, voltages_changed, currents_changed);
    if (BeginTracePlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_ACSweepView.From, &m_ACSweepView.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current phase (deg)", ImPlotAxisFlags_AuxDefault);
        }
        if (steps) {
            std::vector<StepTraces> step_traces;
            for (const Core::ACSweep &step : steps->Results) {
                step_traces.push_back({&step.Frequencies, &step.NodePhasesDegrees, &step.CurrentPhasesDegrees});
            }
            PlotOtherSteps(step_traces, steps->Selected, m_Schematic, show_currents, {});
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees, m_Schematic, " deg", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentPhasesDegrees, m_Schematic, " deg", "", plotted);
        }
        DrawCursors(m_ACSweepCursors, true);
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
}

// Every curve of a family plots under the same label, so a trace keeps one color and one legend entry
void OutputWindow::DrawDCSweep(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math) {
    static const std::vector<MathTrace> no_math;
    const std::vector<MathTrace> &first_math = math.empty() ? no_math : math.front();
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(first_curve.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = m_DCSweepFit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed =
        m_DCSweepFit.TakeNodesChanged(ListMeasuredNodes(first_curve.NodeVoltages.size(), m_Schematic));
    SetNextFits(m_DCSweepFit.TakeNewResult(m_Schematic.GetDCSweepVersion()), voltages_changed, currents_changed);
    if (m_DCSweepFit.TakeMathChanged(ListMathLabels(first_math))) {
        FitMathAxes(first_math);
    }
    const bool stepped = !sweep.SteppedSource.empty();
    if (stepped) {
        ImGui::TextDisabled(
            "%s",
            std::format("One curve per value of {}, labeled where the curves part most", sweep.SteppedSource).c_str());
    }
    if (!BeginTracePlot("##dc", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    const bool has_voltages =
        HasMeasuredVoltage(first_curve.NodeVoltages.size(), m_Schematic) || UsesAxis(first_math, ImAxis_Y1);
    ImPlot::SetupAxes(sweep.SweptSource.c_str(), "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents || !first_math.empty()));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(sweep.SweptUnit.c_str()));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    const std::string third_unit = ThirdAxisUnit(first_math);
    SetupMathAxes(first_math, show_currents, third_unit);
    std::vector<PlottedTrace> plotted;
    for (std::size_t curve_index = 0; curve_index < sweep.Curves.size(); ++curve_index) {
        const Core::DCSweepCurve &curve = sweep.Curves[curve_index];
        const std::string suffix = stepped ? " at " + FormatStepLabel(sweep, curve) : "";
        PlotNodes(sweep.SweptValues, curve.NodeVoltages, m_Schematic, "V", suffix, plotted);
        if (show_currents) {
            PlotCurrents(sweep.SweptValues, curve.Currents, m_Schematic, "A", suffix, plotted);
        }
        if (curve_index < math.size()) {
            PlotMath(sweep.SweptValues, math[curve_index], suffix, plotted);
        }
    }
    if (stepped) {
        LabelCurves(sweep, m_Schematic, show_currents, math);
    }
    DrawCursorReadout(sweep.SweptValues, sweep.SweptUnit.c_str(), false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_DCSweepView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// Densities on log-log axes, as noise is read: the output in the color of its node, and the input-referred noise
// dashed, on the same axis when the input is a voltage source and on the right one in amperes for a current source.
// The parts checked in the panel add their share at the output
void OutputWindow::DrawNoise(const Core::Noise &noise) {
    const bool input_shown = HasInputGain(noise);
    if (!input_shown) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", std::format("{} hardly reaches {} (under -120 dB, as through a logic gate), so its "
                                              "input-referred noise is left out",
                                              noise.InputSource,
                                              GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode))
                                      .c_str());
        ImGui::PopTextWrapPos();
    }
    if (!BeginTracePlot("##noise", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    // ImPlot keeps the format strings until the plot ends
    const std::string output_unit = GetDensityUnit("V");
    const std::string input_unit = GetDensityUnit(noise.InputUnit);
    SetupNoiseAxes(noise, input_shown, output_unit, input_unit);
    std::vector<PlottedTrace> plotted;
    PlotNoiseTraces(noise, m_Schematic.GetConnectivity(), m_ShownNoiseContributions, input_shown, output_unit,
                    input_unit, plotted);
    DrawCursorReadout(noise.Frequencies, "Hz", true, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_NoiseView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// Logarithmic axes, fitted in decades, since the padding of the other plots is linear and would reach below zero:
// every axis for a new result, and the output axis when the plotted shares change. Referred to a voltage source,
// the input noise is in volts too and shares the axis of the output; referred to a current source, it has its own.
// The limits are set during the setup, since ImPlot fits an axis on its own the first frame it is shown
void OutputWindow::SetupNoiseAxes(const Core::Noise &noise, const bool input_shown, const std::string &output_unit,
                                  const std::string &input_unit) {
    const bool new_result = m_NoiseFit.TakeNewResult(m_Schematic.GetNoiseVersion());
    const bool shares_changed =
        TakeTracesChanged(m_PlottedNoiseContributions,
                          std::vector<std::string>(m_ShownNoiseContributions.begin(), m_ShownNoiseContributions.end()));
    const bool input_in_volts = noise.InputUnit == "V";
    ImPlot::SetupAxes("Frequency", "Noise density", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>(output_unit.c_str()));
    if (input_shown && !input_in_volts) {
        ImPlot::SetupAxis(ImAxis_Y2, "Input-referred noise", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisScale(ImAxis_Y2, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>(input_unit.c_str()));
    }
    if (new_result) {
        ImPlot::SetupAxisLimits(ImAxis_X1, noise.Frequencies.front(), noise.Frequencies.back(), ImPlotCond_Always);
        if (input_shown && !input_in_volts) {
            const PlotSpan input_range = GetDecadeFitRange({noise.InputDensities});
            ImPlot::SetupAxisLimits(ImAxis_Y2, input_range.From, input_range.To, ImPlotCond_Always);
        }
    }
    if (!new_result && !shares_changed) {
        return;
    }
    std::vector<std::span<const double>> voltage_traces{noise.OutputDensities};
    if (input_shown && input_in_volts) {
        voltage_traces.emplace_back(noise.InputDensities);
    }
    for (const Core::NoiseContribution &contribution : noise.Contributions) {
        if (m_ShownNoiseContributions.contains(contribution.Name)) {
            voltage_traces.emplace_back(contribution.OutputDensities);
        }
    }
    const PlotSpan voltage_range = GetDecadeFitRange(voltage_traces);
    ImPlot::SetupAxisLimits(ImAxis_Y1, voltage_range.From, voltage_range.To, ImPlotCond_Always);
}

// The span the transient statistics measure: between its cursors, or what its plot shows, or the whole run before
// the plot was first drawn. Clipped to the run, from the earlier end
PlotSpan OutputWindow::GetSpectrumSpan(const Core::Transient &transient) const {
    const PlotSpan measured = MeasuredSpan(m_TransientCursors, m_TransientView);
    const double run_start = transient.Times.front();
    const double run_stop = transient.Times.back();
    const double from = std::max(std::min(measured.From, measured.To), run_start);
    const double to = std::min(std::max(measured.From, measured.To), run_stop);
    if (!(from < to)) {
        return {.From = run_start, .To = run_stop};
    }
    return {.From = from, .To = to};
}

// Transforms the measured traces again only when the result, the span, the window or the traces changed. Every trace
// shares one even grid, so the spectra share their frequencies
void OutputWindow::UpdateSpectra(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    std::vector<PlottedTrace> sources = ListTraces(transient.NodeVoltages, transient.Currents, m_Schematic, "V", "A");
    std::vector<SpectrumTrace> traces;
    for (const PlottedTrace &source : sources) {
        const bool current = std::string_view(source.Unit) == "A";
        traces.push_back({.Label = source.Label,
                          .Unit = source.Unit,
                          .Axis = current ? ImAxis_Y2 : ImAxis_Y1,
                          .Color = source.Color,
                          .Dashed = current,
                          .Decibels = std::nullopt,
                          .LargestAmplitude = 0.0,
                          .Harmonics = {}});
    }
    for (const MathTrace &trace : math) {
        sources.push_back({trace.Label, &trace.Values, trace.Unit.c_str(), trace.Color});
        traces.push_back({.Label = trace.Label,
                          .Unit = trace.Unit,
                          .Axis = trace.Axis,
                          .Color = trace.Color,
                          .Dashed = false,
                          .Decibels = std::nullopt,
                          .LargestAmplitude = 0.0,
                          .Harmonics = {}});
    }
    std::vector<std::string> labels;
    for (const SpectrumTrace &trace : traces) {
        labels.push_back(trace.Label + trace.Unit);
    }
    const PlotSpan span = GetSpectrumSpan(transient);
    const std::size_t version = m_Schematic.GetTransientVersion();
    if (m_Spectra && m_Spectra->TransientVersion == version && m_Spectra->Span.From == span.From &&
        m_Spectra->Span.To == span.To && m_Spectra->Window == m_SpectrumWindow && m_Spectra->Labels == labels) {
        return;
    }

    TransientSpectra spectra{.TransientVersion = version,
                             .Span = span,
                             .Window = m_SpectrumWindow,
                             .Labels = std::move(labels),
                             .Frequencies = {},
                             .Traces = {}};
    const std::size_t sample_count = ChooseSpectrumSampleCount(transient.Times, span.From, span.To);
    for (std::size_t index = 0; index < traces.size(); ++index) {
        SpectrumTrace &trace = traces[index];
        const std::optional<Spectrum> spectrum = ComputeSpectrum(transient.Times, *sources[index].Values, span.From,
                                                                 span.To, sample_count, m_SpectrumWindow);
        if (!spectrum) {
            continue;
        }
        // The mean has no place on a logarithmic axis; the statistics show it instead
        const std::span<const double> above_zero = std::span(spectrum->Amplitudes).subspan(1);
        trace.Decibels = ToDecibels(above_zero);
        for (const double amplitude : spectrum->Amplitudes) {
            trace.LargestAmplitude = std::max(trace.LargestAmplitude, std::abs(amplitude));
        }
        trace.Harmonics = AnalyzeHarmonics(*spectrum, HighestHarmonic);
        if (spectra.Frequencies.empty()) {
            spectra.Frequencies.assign(spectrum->Frequencies.begin() + 1, spectrum->Frequencies.end());
        }
    }
    spectra.Traces = std::move(traces);
    m_Spectra = std::move(spectra);
    m_SpectraChanged = true;
}

// Voltages, currents and math channels keep the axes they have in the transient, in decibels; each trace marks its
// harmonics. A new spectrum fits its axes, and a linear one opens on the harmonics of the first trace that has some
void OutputWindow::DrawSpectrum() {
    const TransientSpectra &spectra = *m_Spectra;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", std::format("From {}s to {}s of the transient: zoom its plot or place its cursors to "
                                          "pick the stretch, such as the steady state",
                                          Core::FormatValue(spectra.Span.From), Core::FormatValue(spectra.Span.To))
                                  .c_str());
    ImGui::PopTextWrapPos();
    if (spectra.Frequencies.empty()) {
        return;
    }
    const bool fit = std::exchange(m_SpectraChanged, false);
    if (fit) {
        const PlotSpan range = GetSpectrumFitRange(spectra, m_SpectrumLogarithmic);
        ImPlot::SetNextAxisLimits(ImAxis_X1, range.From, range.To, ImPlotCond_Always);
        for (const ImAxis axis : {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3}) {
            ImPlot::SetNextAxisToFit(axis);
        }
    }
    if (!BeginTracePlot("##spectrum", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    const auto uses_axis = [&spectra](const ImAxis axis) {
        return std::ranges::any_of(spectra.Traces,
                                   [axis](const SpectrumTrace &trace) { return trace.Decibels && trace.Axis == axis; });
    };
    ImPlot::SetupAxes("Frequency", "Voltage (dBV)", ImPlotAxisFlags_None,
                      VoltageAxisFlags(uses_axis(ImAxis_Y1), uses_axis(ImAxis_Y2) || uses_axis(ImAxis_Y3)));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    if (m_SpectrumLogarithmic) {
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    }
    if (uses_axis(ImAxis_Y2)) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current (dBA)", ImPlotAxisFlags_AuxDefault);
    }
    if (uses_axis(ImAxis_Y3)) {
        ImPlot::SetupAxis(ImAxis_Y3, "Math (dB)", ImPlotAxisFlags_AuxDefault);
    }
    std::vector<PlottedTrace> plotted;
    std::vector<std::string> units;
    units.reserve(spectra.Traces.size());
    for (const SpectrumTrace &trace : spectra.Traces) {
        if (!trace.Decibels) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, trace.Axis);
        if (trace.Dashed) {
            PlotDashedLine(trace.Label, spectra.Frequencies, *trace.Decibels, trace.Color);
        } else {
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
            ImPlot::PlotLine(trace.Label.c_str(), spectra.Frequencies.data(), trace.Decibels->data(),
                             static_cast<int>(spectra.Frequencies.size()), spec);
        }
        std::vector<double> harmonic_frequencies;
        std::vector<double> harmonic_levels;
        for (const Harmonic &harmonic : trace.Harmonics.Harmonics) {
            harmonic_frequencies.push_back(harmonic.Frequency);
            harmonic_levels.push_back(20.0 * std::log10(std::max(harmonic.Amplitude, 1e-15)));
        }
        ImPlotSpec marker_spec;
        marker_spec.LineColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
        marker_spec.Marker = ImPlotMarker_Circle;
        marker_spec.MarkerSize = HarmonicMarkerSize;
        ImPlot::PlotScatter(std::format("##{} harmonics", trace.Label).c_str(), harmonic_frequencies.data(),
                            harmonic_levels.data(), static_cast<int>(harmonic_frequencies.size()), marker_spec);
        units.push_back(GetDecibelUnit(trace));
        plotted.push_back({trace.Label, &*trace.Decibels, units.back().c_str(), trace.Color});
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    DrawCursorReadout(spectra.Frequencies, "Hz", m_SpectrumLogarithmic, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_SpectrumView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// Like the measurements of an oscilloscope, for every shown trace; with the cursors, also the value of each
// trace at both and their difference, and the time between them with its frequency
void OutputWindow::DrawTransientStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    DrawCursorToggle(m_TransientCursors);
    if (!FindLogicNodes(m_Schematic.BuildCircuit()).empty()) {
        ImGui::Checkbox("Logic lanes", &m_ShowLogicLanes);
        ImGui::SetItemTooltip("Plot the signals of gates and flip-flops as lanes under the analog traces");
        if (m_ShowLogicLanes) {
            ImGui::Checkbox("Bit values", &m_ShowBitValues);
            ImGui::SetItemTooltip("Write 0, 1 or X in every cell of a lane, a cell being as wide as the shortest "
                                  "steady stretch, such as half a clock period");
        }
    }
    const PlotCursors &cursors = m_TransientCursors;
    if (cursors.Shown && BeginStatisticsTable("cursors")) {
        DrawStatistic("A", FormatQuantity(cursors.A, "s"));
        DrawStatistic("B", FormatQuantity(cursors.B, "s"));
        DrawStatistic("B-A", FormatQuantity(cursors.B - cursors.A, "s"));
        DrawStatistic("1/(B-A)",
                      cursors.B != cursors.A ? FormatQuantity(1.0 / std::abs(cursors.B - cursors.A), "Hz") : "-");
        ImGui::EndTable();
    }
    std::vector<PlottedTrace> traces = ListTraces(transient.NodeVoltages, transient.Currents, m_Schematic, "V", "A");
    for (const MathTrace &trace : math) {
        traces.push_back({trace.Label, &trace.Values, trace.Unit.c_str(), trace.Color});
    }
    if (traces.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    ImGui::TextDisabled("%s", cursors.Shown ? "Between the cursors" : "Over the visible time");
    const PlotSpan span = MeasuredSpan(cursors, m_TransientView);
    for (const PlottedTrace &trace : traces) {
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", trace.Label.c_str());
        if (!BeginStatisticsTable(trace.Label)) {
            continue;
        }
        // The size of the trace in the span tells residue from real values in every readout
        const auto statistics = ComputeTraceStatistics(transient.Times, *trace.Values, span.From, span.To);
        const double scale = statistics ? std::max(std::abs(statistics->Maximum), std::abs(statistics->Minimum)) : 0.0;
        if (statistics) {
            DrawMeasuredValue("Pk-Pk", statistics->PeakToPeak, scale, trace.Unit);
            DrawMeasuredValue("Max", statistics->Maximum, scale, trace.Unit);
            DrawMeasuredValue("Min", statistics->Minimum, scale, trace.Unit);
            DrawMeasuredValue("Mean", statistics->Mean, scale, trace.Unit);
            DrawMeasuredValue("RMS", statistics->RMS, scale, trace.Unit);
            DrawStatistic("Freq", FormatOptionalQuantity(statistics->Frequency, "Hz"));
            DrawStatistic("Period", statistics->Frequency ? FormatQuantity(1.0 / *statistics->Frequency, "s") : "-");
        }
        if (cursors.Shown) {
            const double at_a = InterpolateAt(transient.Times, *trace.Values, cursors.A, false);
            const double at_b = InterpolateAt(transient.Times, *trace.Values, cursors.B, false);
            DrawMeasuredValue("At A", at_a, scale, trace.Unit);
            DrawMeasuredValue("At B", at_b, scale, trace.Unit);
            DrawMeasuredValue("B-A", at_b - at_a, scale, trace.Unit);
        }
        ImGui::EndTable();
    }
}

// Like the harmonic readout of a spectrum analyzer: the mean, the fundamental and the THD of every shown trace,
// then each harmonic relative to the fundamental, in dBc
void OutputWindow::DrawSpectrumStatistics() {
    DrawSpectrumControls();
    const TransientSpectra &spectra = *m_Spectra;
    if (spectra.Traces.empty()) {
        ImGui::TextDisabled("Measure a trace to see its harmonics");
        return;
    }
    if (spectra.Frequencies.size() > 1) {
        ImGui::TextDisabled("%s",
                            std::format("Resolution {}Hz", Core::FormatValue(spectra.Frequencies.front())).c_str());
    }
    DrawPeriodsWarning(spectra);
    for (const SpectrumTrace &trace : spectra.Traces) {
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", trace.Label.c_str());
        if (!trace.Decibels) {
            ImGui::TextDisabled("Undefined in the stretch");
        } else if (BeginStatisticsTable(trace.Label)) {
            DrawHarmonics(trace);
            ImGui::EndTable();
        }
    }
}

// The window of the transform and the scale of the frequency axis, which refits the plot when it changes
void OutputWindow::DrawSpectrumControls() {
    DrawFieldLabel("Window");
    const char *window_name = m_SpectrumWindow == SpectrumWindow::Hann ? "Hann" : "Rectangular";
    if (ImGui::BeginCombo("##window", window_name)) {
        if (ImGui::Selectable("Hann", m_SpectrumWindow == SpectrumWindow::Hann)) {
            m_SpectrumWindow = SpectrumWindow::Hann;
        }
        ImGui::SetItemTooltip("Suits any stretch, with wider peaks");
        if (ImGui::Selectable("Rectangular", m_SpectrumWindow == SpectrumWindow::Rectangular)) {
            m_SpectrumWindow = SpectrumWindow::Rectangular;
        }
        ImGui::SetItemTooltip("Sharpest peaks, but only for a whole number of periods, such as between cursors one "
                              "period apart");
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Logarithmic frequency", &m_SpectrumLogarithmic)) {
        m_SpectraChanged = true;
    }
}

// Like the measurements of a network analyzer: peak, -3 dB band, unity gain and phase margin for every shown
// trace, always over the whole sweep, since they describe the response and a narrower span would move the peak
// that the -3 dB points refer to; the cursors only read the gain and phase at two frequencies
void OutputWindow::DrawACSweepStatistics(const Core::ACSweep &sweep) {
    DrawCursorToggle(m_ACSweepCursors);
    const PlotCursors &cursors = m_ACSweepCursors;
    if (cursors.Shown && BeginStatisticsTable("cursors")) {
        DrawStatistic("A", FormatQuantity(cursors.A, "Hz"));
        DrawStatistic("B", FormatQuantity(cursors.B, "Hz"));
        DrawStatistic("B/A", cursors.A > 0.0 ? FormatFixed(std::log10(cursors.B / cursors.A), " dec") : "-");
        ImGui::EndTable();
    }
    const std::vector<PlottedTrace> magnitudes =
        ListTraces(sweep.NodeMagnitudesDecibels, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", " dB");
    const std::vector<PlottedTrace> phases =
        ListTraces(sweep.NodePhasesDegrees, sweep.CurrentPhasesDegrees, m_Schematic, " deg", " deg");
    if (magnitudes.empty()) {
        ImGui::TextDisabled("Measure a trace to see its statistics");
        return;
    }
    ImGui::TextDisabled("Response over the whole sweep");
    for (std::size_t index = 0; index < magnitudes.size(); ++index) {
        const PlottedTrace &magnitude = magnitudes[index];
        const std::vector<double> &phase = *phases[index].Values;
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(magnitude.Color), "%s", magnitude.Label.c_str());
        if (!BeginStatisticsTable(magnitude.Label)) {
            continue;
        }
        if (const auto statistics = ComputeBodeStatistics(sweep.Frequencies, *magnitude.Values, phase,
                                                          sweep.Frequencies.front(), sweep.Frequencies.back())) {
            DrawStatistic("Peak", FormatFixed(statistics->PeakGain, " dB"));
            DrawStatistic("at", FormatQuantity(statistics->PeakFrequency, "Hz"));
            DrawStatistic("-3 dB low", FormatOptionalQuantity(statistics->LowerCutoff, "Hz"));
            DrawStatistic("-3 dB high", FormatOptionalQuantity(statistics->UpperCutoff, "Hz"));
            DrawStatistic("0 dB at", FormatOptionalQuantity(statistics->UnityGainFrequency, "Hz"));
            DrawStatistic("Phase margin",
                          statistics->PhaseMargin ? FormatFixed(*statistics->PhaseMargin, " deg") : "-");
        }
        if (cursors.Shown) {
            const double gain_a = InterpolateAt(sweep.Frequencies, *magnitude.Values, cursors.A, true);
            const double gain_b = InterpolateAt(sweep.Frequencies, *magnitude.Values, cursors.B, true);
            const double phase_a = InterpolateAt(sweep.Frequencies, phase, cursors.A, true);
            const double phase_b = InterpolateAt(sweep.Frequencies, phase, cursors.B, true);
            DrawStatistic("Gain at A", FormatFixed(gain_a, " dB"));
            DrawStatistic("Gain at B", FormatFixed(gain_b, " dB"));
            DrawStatistic("Gain B-A", FormatFixed(gain_b - gain_a, " dB"));
            DrawStatistic("Phase at A", FormatFixed(phase_a, " deg"));
            DrawStatistic("Phase at B", FormatFixed(phase_b, " deg"));
            DrawStatistic("Phase B-A", FormatFixed(phase_b - phase_a, " deg"));
        }
        ImGui::EndTable();
    }
}

// The RMS noise over the whole range, at the output and at the input, then the share of every part in the output
// noise, largest first; checking a part plots its share
void OutputWindow::DrawNoiseStatistics(const Core::Noise &noise) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", std::format("RMS from {}", FormatFrequencyRange(noise)).c_str());
    ImGui::PopTextWrapPos();
    if (BeginStatisticsTable("noise_totals")) {
        const std::string output_name =
            std::format("Output {}", GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode));
        DrawStatistic(output_name.c_str(), FormatQuantity(noise.OutputTotal, "V"));
        const std::string input_name = std::format("Input {}", noise.InputSource);
        DrawStatistic(input_name.c_str(),
                      HasInputGain(noise) ? FormatQuantity(noise.InputTotal, noise.InputUnit.c_str()) : "-");
        ImGui::SetItemTooltip("The noise a source at the input would need to cause the same noise at the output");
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Share of each part at the output");
    if (noise.Contributions.empty()) {
        ImGui::TextDisabled("No part adds noise");
        return;
    }
    const std::optional<bool> show_all = DrawAllNoneButtons("contributions");
    if (!ImGui::BeginTable("noise_contributions", 3, ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    const double output_power = noise.OutputTotal * noise.OutputTotal;
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (show_all) {
            if (*show_all) {
                m_ShownNoiseContributions.insert(contribution.Name);
            } else {
                m_ShownNoiseContributions.erase(contribution.Name);
            }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool shown = m_ShownNoiseContributions.contains(contribution.Name);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(index));
        if (ImGui::Checkbox(contribution.Name.c_str(), &shown)) {
            if (shown) {
                m_ShownNoiseContributions.insert(contribution.Name);
            } else {
                m_ShownNoiseContributions.erase(contribution.Name);
            }
        }
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatQuantity(contribution.OutputTotal, "V").c_str());
        ImGui::TableNextColumn();
        // Powers add up, so the shares of all parts make the whole
        const double share =
            output_power > 0.0 ? contribution.OutputTotal * contribution.OutputTotal / output_power : 0.0;
        ImGui::TextUnformatted(FormatFixed(share * 100.0, "%").c_str());
    }
    ImGui::EndTable();
}

// A plain button apart from the tabs, so it does not read as another page; its popup hangs below it, aligned to its
// right edge, so it stays inside the window
void OutputWindow::DrawExportButton(const ImVec2 tab_row, const float tab_row_width, const float export_width,
                                    const FrameResults &frame) {
    ImGui::SetCursorScreenPos({tab_row.x + tab_row_width - export_width, tab_row.y});
    if (ImGui::Button(ExportLabel)) {
        m_Exporter.OpenPopup();
    }
    ImGui::SetItemTooltip("Save the plot of this tab as an SVG image or a CSV table");
    ImGui::SetNextWindowPos({ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y}, ImGuiCond_Appearing,
                            ImVec2(1.0f, 0.0f));
    if (const std::optional<ExportFormat> format = m_Exporter.DrawPopup(GetTabTitle(m_ShownTab), CanExport(frame))) {
        m_Exporter.Start(BuildShownFigure(frame), *format, m_Schematic.GetFilePath(), GetTabFileSuffix(m_ShownTab));
    }
}

// Whether the shown tab has something to export: a result with a measured trace or a math channel
bool OutputWindow::CanExport(const FrameResults &frame) const {
    switch (m_ShownTab) {
    case PlotTab::OperatingPoint:
        return false;
    case PlotTab::Transient:
        return m_Schematic.GetTransient() && (frame.AnythingMeasured || !frame.TransientMath.empty());
    case PlotTab::Spectrum:
        return m_Schematic.GetTransient() && m_Spectra &&
               std::ranges::any_of(m_Spectra->Traces,
                                   [](const SpectrumTrace &trace) { return trace.Decibels.has_value(); });
    case PlotTab::ACSweep:
        return m_Schematic.GetACSweep() && frame.AnythingMeasured;
    case PlotTab::DCSweep:
        return m_Schematic.GetDCSweep() &&
               (frame.AnythingMeasured || (!frame.DCSweepMath.empty() && !frame.DCSweepMath.front().empty()));
    case PlotTab::Noise:
        return m_Schematic.GetNoise().has_value();
    case PlotTab::TransferFunction:
        return false;
    }
    return false;
}

// The figure of the shown tab, which CanExport() has found something to export in
ExportFigure OutputWindow::BuildShownFigure(const FrameResults &frame) const {
    switch (m_ShownTab) {
    case PlotTab::Transient: {
        const Core::Transient &transient = *m_Schematic.GetTransient();
        return BuildTransientFigure(transient, frame.TransientMath, FindMeasuredLanes(transient.NodeVoltages.size()),
                                    m_Schematic, m_ShowBitValues, m_TransientView);
    }
    case PlotTab::Spectrum:
        return BuildSpectrumFigure(*m_Spectra, m_SpectrumLogarithmic, m_SpectrumView);
    case PlotTab::ACSweep:
        return BuildACSweepFigure(*m_Schematic.GetACSweep(), m_Schematic, m_ACSweepView);
    case PlotTab::DCSweep:
        return BuildDCSweepFigure(*m_Schematic.GetDCSweep(), frame.DCSweepMath, m_Schematic, m_DCSweepView);
    case PlotTab::Noise:
        return BuildNoiseFigure(*m_Schematic.GetNoise(), m_Schematic, m_ShownNoiseContributions, m_NoiseView);
    case PlotTab::OperatingPoint:
    case PlotTab::TransferFunction:
        break;
    }
    return {};
}

} // namespace GUI
