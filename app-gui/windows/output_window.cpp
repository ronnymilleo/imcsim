/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#include "output_window.h"

#include "implot.h"
#include "implot_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "node_colors.h"
#include "plot_helpers.h"
#include "spice_value.h"
#include "theme.h"
#include "trace_statistics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

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
constexpr ImU32 CursorLineColor = IM_COL32(255, 255, 255, 90);
// Width of the statistics panel, in font sizes
constexpr float StatisticsWidth = 15.0f;
// Cursors A and B are placed at these fractions of the visible range when turned on
constexpr double CursorAPlacement = 1.0 / 3.0;
constexpr double CursorBPlacement = 2.0 / 3.0;
constexpr ImVec4 CursorAColor = {1.0f, 0.78f, 0.3f, 1.0f};
constexpr ImVec4 CursorBColor = {0.45f, 0.8f, 1.0f, 1.0f};
// Math channels take their own colors, apart from those of nodes and currents, and a heavier line
constexpr auto MathColors = std::to_array<ImU32>({
    IM_COL32(230, 110, 230, 255),
    IM_COL32(150, 230, 90, 255),
    IM_COL32(235, 235, 235, 255),
    IM_COL32(90, 220, 190, 255),
});
constexpr float MathLineWeight = 2.0f;
// Widths in the math editor, in font sizes
constexpr float OperandWidth = 10.0f;
constexpr float ExpressionWidth = 20.0f;
constexpr auto MathOperators = std::to_array<MathOperator>({
    MathOperator::Add,
    MathOperator::Subtract,
    MathOperator::Multiply,
    MathOperator::Divide,
});

/**
 * @struct  PlottedTrace
 * @brief   A trace drawn in a plot, kept so the cursor readout can list its value at any sample.
 */
struct PlottedTrace {
    std::string Label;
    const std::vector<double> *Values;
    const char *Unit;
    ImU32 Color;
};

// Axis ticks use the same suffixes as the values the user types, such as "10m" or "1k", followed by the unit.
// ImPlot hands the unit back as untyped user data, which is why callers cast away the const of the literal
int FormatAxisValue(const double value, char *buffer, const int size, void *unit) {
    const std::string text = std::format("{}{}", Core::FormatValue(value), static_cast<const char *>(unit));
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s", text.c_str());
}

// Splits the room left in the window between the plots
float PlotHeight(const int plot_count) {
    return std::max(ImGui::GetFontSize() * MinPlotHeight,
                    ImGui::GetContentRegionAvail().y / static_cast<float>(plot_count) -
                        ImGui::GetStyle().ItemSpacing.y);
}

// Ground is always 0 V, so plots start at node 1. Each node keeps its color whichever nodes are measured. The
// suffix tells the curves of a DC sweep family apart in the cursor readout
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values,
               const Schematic &schematic, const char *unit, const std::string &suffix,
               std::vector<PlottedTrace> &plotted) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        const ImU32 color = GetNodeColor(static_cast<int>(node));
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
        const std::string label = std::format("V({})", node);
        ImPlot::PlotLine(label.c_str(), xs.data(), node_values[node].data(), static_cast<int>(xs.size()), spec);
        plotted.push_back({label + suffix, &node_values[node], unit, color});
    }
}

// ImPlot draws no dashes, so the line is drawn by hand: a dummy item gives the legend entry, which can still hide
// it, and an invisible line keeps the automatic fit of the axes
void PlotDashedLine(const std::string &label, const std::vector<double> &xs, const std::vector<double> &ys,
                    const ImU32 color) {
    ImPlotSpec legend_spec;
    legend_spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
    ImPlot::PlotDummy(label.c_str(), legend_spec);
    if (const ImPlotItem *item = ImPlot::GetItem(label.c_str()); item != nullptr && !item->Show) {
        return;
    }
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

bool HasMeasuredCurrent(const std::vector<Core::ComponentTrace> &currents, const Schematic &schematic) {
    return std::ranges::any_of(currents, [&schematic](const Core::ComponentTrace &current) {
        return schematic.IsCurrentMeasured(current.Name);
    });
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
                                       CursorLineColor);
    ImPlot::PopPlotClipRect();

    ImGui::BeginTooltip();
    ImGui::TextDisabled("%s", std::format("at {}{}", Core::FormatValue(xs[sample]), x_unit).c_str());
    for (const PlottedTrace &trace : plotted) {
        const std::string text =
            std::format("{}: {}{}", trace.Label, Core::FormatValue((*trace.Values)[sample]), trace.Unit);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", text.c_str());
    }
    ImGui::EndTooltip();
}

// Labels every curve of each shown trace with the value of the stepped source, so the curves of a family tell
// apart. The labels of a trace go where its curves are furthest apart, which is the end for most traces but the
// peak for a derivative, whose curves all end near zero. Each label sits on the axis of its curve
void LabelCurves(const Core::DCSweep &sweep, const Schematic &schematic, const bool show_currents,
                 const std::vector<std::vector<MathTrace>> &math) {
    std::vector<std::string> labels;
    for (const Core::DCSweepCurve &curve : sweep.Curves) {
        labels.push_back(
            std::format("{}={}{}", sweep.SteppedSource, Core::FormatValue(curve.StepValue), sweep.SteppedUnit));
    }
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    const auto label_family = [&](const ImAxis axis, const std::vector<const std::vector<double> *> &curves) {
        const std::size_t sample = FindWidestSpread(curves);
        if (sample >= sweep.SweptValues.size()) {
            return;
        }
        ImPlot::SetAxes(ImAxis_X1, axis);
        for (std::size_t curve = 0; curve < curves.size(); ++curve) {
            const double y = (*curves[curve])[sample];
            if (std::isfinite(y)) {
                ImPlot::Annotation(sweep.SweptValues[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s",
                                   labels[curve].c_str());
            }
        }
    };

    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            curves.push_back(&curve.NodeVoltages[node]);
        }
        label_family(ImAxis_Y1, curves);
    }
    for (std::size_t current = 0; show_currents && current < first_curve.Currents.size(); ++current) {
        if (!schematic.IsCurrentMeasured(first_curve.Currents[current].Name)) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            curves.push_back(&curve.Currents[current].Values);
        }
        label_family(ImAxis_Y2, curves);
    }
    // Every curve evaluates the same channels, so the traces of one channel share an index across curves
    const std::size_t math_count = math.size() == sweep.Curves.size() ? math.front().size() : 0;
    for (std::size_t trace = 0; trace < math_count; ++trace) {
        std::vector<const std::vector<double> *> curves;
        for (const std::vector<MathTrace> &curve_math : math) {
            if (trace < curve_math.size()) {
                curves.push_back(&curve_math[trace].Values);
            }
        }
        if (curves.size() == sweep.Curves.size()) {
            label_family(math.front()[trace].Axis, curves);
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
std::vector<int> ListMeasuredNodes(const std::size_t node_count, const Schematic &schematic) {
    std::vector<int> nodes;
    for (std::size_t node = 1; node < node_count; ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            nodes.push_back(static_cast<int>(node));
        }
    }
    return nodes;
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

// Returns whether the axis of some traces must fit because the measured ones changed: a new trace could otherwise
// sit far off the scale of the old ones, and a new axis has no meaningful range yet. Removing every trace fits
// nothing, since there is nothing left to fit to
template <typename Key> bool TakeTracesChanged(std::vector<Key> &shown, std::vector<Key> measured) {
    const bool changed = measured != shown;
    shown = std::move(measured);
    return changed && !shown.empty();
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

// Returns whether the axes must fit, which is the case once for every new result
bool TakeFit(std::optional<std::size_t> &fitted_version, const std::size_t version) {
    return std::exchange(fitted_version, version) != version;
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
                                     const std::vector<Core::ComponentTrace> &currents, const Schematic &schematic,
                                     const char *voltage_unit, const char *current_unit) {
    std::vector<PlottedTrace> traces;
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
            traces.push_back(
                {std::format("V({})", node), &node_values[node], voltage_unit, GetNodeColor(static_cast<int>(node))});
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
    ImPlot::DragLineX(0, &cursors.A, CursorAColor, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.A, CursorAColor, "A");
    ImPlot::DragLineX(1, &cursors.B, CursorBColor, 1.0f, ImPlotDragToolFlags_NoFit);
    ImPlot::TagX(cursors.B, CursorBColor, "B");
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
    return std::format("{:.2f}{}", value, unit);
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

// Voltages and currents share the axes of the probed traces; any other unit, or none, goes on the third axis
ImAxis AxisForUnit(const Dimension unit) {
    if (unit == Dimension{.Volts = 1}) {
        return ImAxis_Y1;
    }
    if (unit == Dimension{.Amperes = 1}) {
        return ImAxis_Y2;
    }
    return ImAxis_Y3;
}

bool UsesAxis(const std::vector<MathTrace> &math, const ImAxis axis) {
    return std::ranges::contains(math, axis, &MathTrace::Axis);
}

// Unit of the third axis: the one its traces share, or none when they differ or have none
std::string ThirdAxisUnit(const std::vector<MathTrace> &math) {
    std::optional<std::string> unit;
    for (const MathTrace &trace : math) {
        if (trace.Axis != ImAxis_Y3) {
            continue;
        }
        if (unit && *unit != trace.Unit) {
            return "";
        }
        unit = trace.Unit;
    }
    return unit.value_or("");
}

std::vector<std::string> ListMathLabels(const std::vector<MathTrace> &math) {
    std::vector<std::string> labels;
    for (const MathTrace &trace : math) {
        labels.push_back(trace.Label + trace.Unit);
    }
    return labels;
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

std::string GetChannelExpression(const MathChannel &channel) {
    if (channel.UsesExpression) {
        return channel.Expression;
    }
    return BuildOperatorExpression(channel.First, channel.Operator, channel.Second);
}

void CompileChannel(MathChannel &channel) {
    channel.Program = CompileExpression(GetChannelExpression(channel));
}

// Returns whether the user picked another trace
bool DrawOperandCombo(const char *id, std::string &operand, const std::vector<std::string> &operands) {
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * OperandWidth);
    if (ImGui::BeginCombo(id, operand.empty() ? "(none)" : operand.c_str())) {
        for (const std::string &candidate : operands) {
            if (ImGui::Selectable(candidate.c_str(), candidate == operand)) {
                operand = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose results are plotted; it must outlive the window.
 */
OutputWindow::OutputWindow(Schematic &schematic) : AppWindow("Output", true), m_Schematic(schematic) {
    SetInitialSize(InitialSize);
}

void OutputWindow::Draw() {
    const auto &transient = m_Schematic.GetTransient();
    const auto &sweep = m_Schematic.GetACSweep();
    const auto &dc_sweep = m_Schematic.GetDCSweep();
    // Every result comes from the same circuit, so they have the same nodes and components
    const std::size_t node_count =
        std::max({transient ? transient->NodeVoltages.size() : 0, sweep ? sweep->NodeMagnitudesDecibels.size() : 0,
                  dc_sweep ? dc_sweep->Curves.front().NodeVoltages.size() : 0});
    static const std::vector<Core::ComponentTrace> no_currents;
    const std::vector<Core::ComponentTrace> *currents = &no_currents;
    if (transient) {
        currents = &transient->Currents;
    } else if (sweep) {
        currents = &sweep->CurrentMagnitudesDecibels;
    } else if (dc_sweep) {
        currents = &dc_sweep->Curves.front().Currents;
    }
    if (node_count > 1 || !currents->empty()) {
        DrawTraceList(node_count, *currents);
        ImGui::SameLine();
    }

    if (!ImGui::BeginChild("plots") || !ImGui::BeginTabBar("plots")) {
        ImGui::EndChild();
        return;
    }
    // Math channels are evaluated once per frame and result, for both the plot and its statistics
    std::vector<std::string> transient_errors;
    const std::vector<MathTrace> transient_math =
        transient ? EvaluateMath(transient->Times, {.Seconds = 1}, transient->NodeVoltages, transient->Currents,
                                 transient_errors)
                  : std::vector<MathTrace>{};
    std::vector<std::string> dc_sweep_errors;
    std::vector<std::vector<MathTrace>> dc_sweep_math;
    if (dc_sweep) {
        const Dimension swept_unit = dc_sweep->SweptUnit == "A" ? Dimension{.Amperes = 1} : Dimension{.Volts = 1};
        for (const Core::DCSweepCurve &curve : dc_sweep->Curves) {
            // Every curve has the same nodes and currents, so the first one reports the errors of all
            std::vector<std::string> curve_errors;
            dc_sweep_math.push_back(
                EvaluateMath(dc_sweep->SweptValues, swept_unit, curve.NodeVoltages, curve.Currents, curve_errors));
            if (dc_sweep_math.size() == 1) {
                dc_sweep_errors = std::move(curve_errors);
            }
        }
    }
    // A new result starts with empty plots until the user picks what to measure
    const bool anything_measured =
        HasMeasuredVoltage(node_count, m_Schematic) || HasMeasuredCurrent(*currents, m_Schematic);
    const char *nothing_measured =
        "Nothing is measured yet: pick nodes and parts with Probe in the editor, or check traces in the list";
    // Applies to the automatic fit of every new result and to the user's double-click fit
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, FitPadding);
    if (ImGui::BeginTabItem("Transient")) {
        if (transient) {
            DrawWithStatistics(
                [&] {
                    if (!anything_measured && transient_math.empty()) {
                        ImGui::TextDisabled("%s", nothing_measured);
                    }
                    DrawMathErrors(transient_errors);
                    DrawTransient(*transient, transient_math);
                },
                [&] { DrawTransientStatistics(*transient, transient_math); });
        } else {
            ImGui::TextDisabled("No transient for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        if (sweep) {
            DrawWithStatistics(
                [&] {
                    ImGui::TextDisabled("%s", anything_measured
                                                  ? "Relative to the AC sources: an amplitude of 1 V reads as gain"
                                                  : nothing_measured);
                    if (!m_MathChannels.empty()) {
                        ImGui::TextDisabled("Math channels are not calculated for AC sweeps yet");
                    }
                    DrawACSweep(*sweep);
                },
                [&] { DrawACSweepStatistics(*sweep); });
        } else {
            ImGui::TextDisabled("No AC sweep for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("DC sweep")) {
        if (dc_sweep) {
            if (!anything_measured && (dc_sweep_math.empty() || dc_sweep_math.front().empty())) {
                ImGui::TextDisabled("%s", nothing_measured);
            }
            DrawMathErrors(dc_sweep_errors);
            DrawDCSweep(*dc_sweep, dc_sweep_math);
        } else {
            ImGui::TextDisabled("No DC sweep for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
    ImGui::EndChild();
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
            if (ImGui::Checkbox(std::format("V({})", node).c_str(), &measured)) {
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
    DrawMathChannels(operands);
    ImGui::EndChild();
}

void OutputWindow::DrawTransient(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    std::vector<std::string> measured_currents = ListMeasuredCurrents(transient.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = TakeTracesChanged(m_TransientShownCurrents, std::move(measured_currents));
    const bool voltages_changed =
        TakeTracesChanged(m_TransientShownNodes, ListMeasuredNodes(transient.NodeVoltages.size(), m_Schematic));
    SetNextFits(TakeFit(m_FittedTransient, m_Schematic.GetTransientVersion()), voltages_changed, currents_changed);
    if (TakeTracesChanged(m_TransientShownMath, ListMathLabels(math))) {
        FitMathAxes(math);
    }
    if (!ImPlot::BeginPlot("##transient", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    const bool has_voltages =
        HasMeasuredVoltage(transient.NodeVoltages.size(), m_Schematic) || UsesAxis(math, ImAxis_Y1);
    ImPlot::SetupAxes("Time", "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents || !math.empty()));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    const std::string third_unit = ThirdAxisUnit(math);
    SetupMathAxes(math, show_currents, third_unit);
    std::vector<PlottedTrace> plotted;
    PlotNodes(transient.Times, transient.NodeVoltages, m_Schematic, "V", "", plotted);
    if (show_currents) {
        PlotCurrents(transient.Times, transient.Currents, m_Schematic, "A", "", plotted);
    }
    PlotMath(transient.Times, math, "", plotted);
    DrawCursors(m_TransientCursors, false);
    DrawCursorReadout(transient.Times, "s", false, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_TransientView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
}

// Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
// their phases follow so each current stays on the same side in both plots. Both plots share their frequency
// range and cursors, so zooming one zooms the other
void OutputWindow::DrawACSweep(const Core::ACSweep &sweep) {
    const bool fit = TakeFit(m_FittedACSweep, m_Schematic.GetACSweepVersion());
    std::vector<std::string> measured_currents = ListMeasuredCurrents(sweep.CurrentMagnitudesDecibels, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = TakeTracesChanged(m_ACSweepShownCurrents, std::move(measured_currents));
    const bool voltages_changed =
        TakeTracesChanged(m_ACSweepShownNodes, ListMeasuredNodes(sweep.NodeMagnitudesDecibels.size(), m_Schematic));
    const ImPlotAxisFlags voltage_axis_flags =
        VoltageAxisFlags(HasMeasuredVoltage(sweep.NodeMagnitudesDecibels.size(), m_Schematic), show_currents);
    const float height = PlotHeight(2);
    SetNextFits(fit, voltages_changed, currents_changed);
    if (ImPlot::BeginPlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Voltage (dB)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_ACSweepView.From, &m_ACSweepView.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current (dB)", ImPlotAxisFlags_AuxDefault);
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        }
        DrawCursors(m_ACSweepCursors, true);
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
    SetNextFits(fit, voltages_changed, currents_changed);
    if (ImPlot::BeginPlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)", ImPlotAxisFlags_None, voltage_axis_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLinks(ImAxis_X1, &m_ACSweepView.From, &m_ACSweepView.To);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current phase (deg)", ImPlotAxisFlags_AuxDefault);
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
    const bool currents_changed = TakeTracesChanged(m_DCSweepShownCurrents, std::move(measured_currents));
    const bool voltages_changed =
        TakeTracesChanged(m_DCSweepShownNodes, ListMeasuredNodes(first_curve.NodeVoltages.size(), m_Schematic));
    SetNextFits(TakeFit(m_FittedDCSweep, m_Schematic.GetDCSweepVersion()), voltages_changed, currents_changed);
    if (TakeTracesChanged(m_DCSweepShownMath, ListMathLabels(first_math))) {
        FitMathAxes(first_math);
    }
    const bool stepped = !sweep.SteppedSource.empty();
    if (stepped) {
        ImGui::TextDisabled(
            "%s",
            std::format("One curve per value of {}, labeled where the curves part most", sweep.SteppedSource).c_str());
    }
    if (!ImPlot::BeginPlot("##dc", ImVec2(-1.0f, PlotHeight(1)))) {
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
        const std::string suffix = stepped ? std::format(" at {}={}{}", sweep.SteppedSource,
                                                         Core::FormatValue(curve.StepValue), sweep.SteppedUnit)
                                           : "";
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
    ImPlot::EndPlot();
}

// Like the measurements of an oscilloscope, for every shown trace; with the cursors, also the value of each
// trace at both and their difference, and the time between them with its frequency
void OutputWindow::DrawTransientStatistics(const Core::Transient &transient, const std::vector<MathTrace> &math) {
    DrawCursorToggle(m_TransientCursors);
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

// Each channel shows its check box, an Edit button and, when its expression does not compile, a warning mark;
// a new channel opens its editor right away
void OutputWindow::DrawMathChannels(const std::vector<std::string> &operands) {
    ImGui::TextDisabled("Math");
    bool open_new_editor = false;
    if (ImGui::SmallButton("Add")) {
        MathChannel channel;
        channel.Name = std::format("M{}", m_NextMathNumber++);
        channel.First = operands.empty() ? "" : operands.front();
        channel.Second = operands.size() > 1 ? operands[1] : channel.First;
        CompileChannel(channel);
        m_MathChannels.push_back(std::move(channel));
        open_new_editor = true;
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < m_MathChannels.size(); ++index) {
        MathChannel &channel = m_MathChannels[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, MathColors[index % MathColors.size()]);
        ImGui::Checkbox(channel.Name.c_str(), &channel.Shown);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", GetChannelExpression(channel).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit") || (open_new_editor && index + 1 == m_MathChannels.size())) {
            ImGui::OpenPopup("math_editor");
        }
        if (!channel.Program) {
            ImGui::SameLine();
            ImGui::TextColored(GetErrorTextColor(), "!");
            ImGui::SetItemTooltip("%s", channel.Program.error().c_str());
        }
        if (ImGui::BeginPopup("math_editor")) {
            if (!DrawMathEditor(channel, operands)) {
                removed = index;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removed) {
        m_MathChannels.erase(m_MathChannels.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
}

// Like the math menu of an oscilloscope: two traces and an operator button, or a typed expression. Switching to
// an expression starts from the operation, so it can be extended. Returns false when the channel is removed
bool OutputWindow::DrawMathEditor(MathChannel &channel, const std::vector<std::string> &operands) {
    ImGui::TextUnformatted(channel.Name.c_str());
    bool changed = false;
    if (ImGui::RadioButton("Operation", !channel.UsesExpression)) {
        channel.UsesExpression = false;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Expression", channel.UsesExpression) && !channel.UsesExpression) {
        channel.Expression = GetChannelExpression(channel);
        channel.UsesExpression = true;
        changed = true;
    }

    if (channel.UsesExpression) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * ExpressionWidth);
        changed |= ImGui::InputText("##expression", &channel.Expression);
        ImGui::TextDisabled("V(2), V(1,2), I(R1), I(Q1.C), numbers such as 1k");
        ImGui::TextDisabled("+ - * / ( ), abs(), sqrt(), ddt() and integ()");
    } else {
        changed |= DrawOperandCombo("##first", channel.First, operands);
        for (std::size_t index = 0; index < MathOperators.size(); ++index) {
            const MathOperator math_operator = MathOperators[index];
            if (index > 0) {
                ImGui::SameLine();
            }
            const char *symbol = GetOperatorSymbol(math_operator);
            if (channel.Operator == math_operator ? PrimaryButton(symbol) : ImGui::Button(symbol)) {
                channel.Operator = math_operator;
                changed = true;
            }
        }
        changed |= DrawOperandCombo("##second", channel.Second, operands);
    }
    if (changed) {
        CompileChannel(channel);
    }
    if (!channel.Program) {
        ImGui::TextColored(GetErrorTextColor(), "%s", channel.Program.error().c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Remove")) {
        ImGui::CloseCurrentPopup();
        return false;
    }
    return true;
}

// Channels that are hidden or do not compile are left out; those that name a node or part the result lacks add
// an error instead of a trace
std::vector<MathTrace> OutputWindow::EvaluateMath(const std::span<const double> xs, const Dimension x_unit,
                                                  const std::vector<std::vector<double>> &node_voltages,
                                                  const std::vector<Core::ComponentTrace> &currents,
                                                  std::vector<std::string> &errors) const {
    std::vector<MathTrace> traces;
    for (std::size_t index = 0; index < m_MathChannels.size(); ++index) {
        const MathChannel &channel = m_MathChannels[index];
        if (!channel.Shown || !channel.Program) {
            continue;
        }
        auto result = EvaluateExpression(*channel.Program, xs, x_unit, node_voltages, currents);
        if (!result) {
            errors.push_back(std::format("{}: {}", channel.Name, result.error()));
            continue;
        }
        traces.push_back({.Label = std::format("{}: {}", channel.Name, GetChannelExpression(channel)),
                          .Values = std::move(result->Values),
                          .Unit = FormatUnit(result->Unit),
                          .Axis = AxisForUnit(result->Unit),
                          .Color = MathColors[index % MathColors.size()]});
    }
    return traces;
}

} // namespace GUI
