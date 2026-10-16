/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#include "output_window.h"

#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include "logic_trace.h"
#include "misc/cpp/imgui_stdlib.h"
#include "node_colors.h"
#include "plot_helpers.h"
#include "probing.h"
#include "schematic_file.h"
#include "spice_value.h"
#include "theme.h"
#include "trace_statistics.h"
#include "value_field.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
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
// Math channels take their own colors, apart from those of nodes and currents, and a heavier line
constexpr auto MathColors = std::to_array<ImU32>({
    IM_COL32(230, 110, 230, 255),
    IM_COL32(150, 230, 90, 255),
    IM_COL32(235, 235, 235, 255),
    IM_COL32(90, 220, 190, 255),
});
constexpr float MathLineWeight = 2.0f;
// The plot of logic lanes: its height is a margin for the axis and a row per lane, in font sizes, and at most this
// share of the room
constexpr float LanePlotMargin = 3.5f;
constexpr float LaneRowHeight = 2.2f;
constexpr float MaxLaneShare = 0.6f;
// Bit labels: the room around the digit, in font sizes, and the corner rounding of its box
constexpr float BitBoxPadding = 0.25f;
constexpr float BitBoxRounding = 3.0f;
// The share of the time an export shows that a cell needs for its bit label
constexpr double MinBitLabelShare = 0.03;
// Room above and below the stack of lanes, in lane units
constexpr double LaneMargin = 0.3;
// Widths in the math editor, in font sizes
constexpr float OperandWidth = 10.0f;
constexpr float ExpressionWidth = 20.0f;
constexpr auto MathOperators = std::to_array<MathOperator>({
    MathOperator::Add,
    MathOperator::Subtract,
    MathOperator::Multiply,
    MathOperator::Divide,
});
constexpr const char *ExportLabel = "Export...";
constexpr const char *ExportPopup = "export";
constexpr const char *ExportErrorPopup = "Could not export the plot";
// Width of the size fields of the export popup, in font sizes
constexpr float ExportSizeWidth = 7.0f;
constexpr std::array<SDL_DialogFileFilter, 1> SVGFilters = {{{"SVG image", "svg"}}};
constexpr std::array<SDL_DialogFileFilter, 1> CSVFilters = {{{"CSV table", "csv"}}};
// The harmonics the spectrum measures, and how many the plot shows when it fits: a linear axis spans them, plus one
// harmonic of room, from 0 Hz
constexpr int HighestHarmonic = 10;
constexpr float HarmonicMarkerSize = 3.0f;
constexpr double MinShownDistortionPercent = 0.001;
// The input-referred noise takes a color of its own, apart from the node colors the output uses
constexpr ImU32 InputNoiseColor = IM_COL32(150, 230, 90, 255);
// Six decades below the largest density, about where the share of a part stops mattering
constexpr double DecadeFitFloor = 1e-6;
// The first bins above 0 Hz, where the Hann window spreads the mean
constexpr std::ptrdiff_t SpectrumSkirtBins = 2;

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
 * @struct  CurveFamily
 * @brief   The curves of one trace across the steps of a DC sweep, and the axis they are plotted on.
 */
struct CurveFamily {
    ImAxis Axis;
    std::vector<const std::vector<double> *> Curves;
};

// Every shown trace of a stepped DC sweep, with one curve per step
std::vector<CurveFamily> ListCurveFamilies(const Core::DCSweep &sweep, const Schematic &schematic,
                                           const bool show_currents, const std::vector<std::vector<MathTrace>> &math) {
    std::vector<CurveFamily> families;
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node))) {
            continue;
        }
        CurveFamily family{.Axis = ImAxis_Y1, .Curves = {}};
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            family.Curves.push_back(&curve.NodeVoltages[node]);
        }
        families.push_back(std::move(family));
    }
    for (std::size_t current = 0; show_currents && current < first_curve.Currents.size(); ++current) {
        if (!schematic.IsCurrentMeasured(first_curve.Currents[current].Name)) {
            continue;
        }
        CurveFamily family{.Axis = ImAxis_Y2, .Curves = {}};
        for (const Core::DCSweepCurve &curve : sweep.Curves) {
            family.Curves.push_back(&curve.Currents[current].Values);
        }
        families.push_back(std::move(family));
    }
    // Every curve evaluates the same channels, so the traces of one channel share an index across curves
    const std::size_t math_count = math.size() == sweep.Curves.size() ? math.front().size() : 0;
    for (std::size_t trace = 0; trace < math_count; ++trace) {
        CurveFamily family{.Axis = math.front()[trace].Axis, .Curves = {}};
        for (const std::vector<MathTrace> &curve_math : math) {
            if (trace < curve_math.size()) {
                family.Curves.push_back(&curve_math[trace].Values);
            }
        }
        if (family.Curves.size() == sweep.Curves.size()) {
            families.push_back(std::move(family));
        }
    }
    return families;
}

std::string FormatStepLabel(const Core::DCSweep &sweep, const Core::DCSweepCurve &curve) {
    return std::format("{}={}{}", sweep.SteppedSource, Core::FormatValue(curve.StepValue), sweep.SteppedUnit);
}

// Labels every curve of each shown trace with the value of the stepped source, so the curves of a family tell
// apart. The labels of a trace go where its curves are furthest apart, which is the end for most traces but the
// peak for a derivative, whose curves all end near zero. Each label sits on the axis of its curve
void LabelCurves(const Core::DCSweep &sweep, const Schematic &schematic, const bool show_currents,
                 const std::vector<std::vector<MathTrace>> &math) {
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    for (const CurveFamily &family : ListCurveFamilies(sweep, schematic, show_currents, math)) {
        const std::size_t sample = FindWidestSpread(family.Curves);
        if (sample >= sweep.SweptValues.size()) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, family.Axis);
        for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
            const double y = (*family.Curves[curve])[sample];
            if (std::isfinite(y)) {
                ImPlot::Annotation(sweep.SweptValues[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s",
                                   FormatStepLabel(sweep, sweep.Curves[curve]).c_str());
            }
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

std::vector<int> ListNodes(const std::vector<LogicNode> &lanes) {
    std::vector<int> nodes;
    for (const LogicNode &lane : lanes) {
        nodes.push_back(lane.Node);
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

ExportColor ToExportColor(const ImU32 color) {
    return {static_cast<std::uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF)};
}

/**
 * @struct  ExportAxes
 * @brief   How the voltage, current and third axes of a plot are described in an exported figure.
 */
struct ExportAxes {
    ExportAxis Voltage;
    ExportAxis Current;
    ExportAxis Third;
};

ExportAxes GetLinearExportAxes(const std::string &third_unit) {
    return {.Voltage = {.Name = "Voltage", .Unit = "V"},
            .Current = {.Name = "Current", .Unit = "A"},
            .Third = {.Name = third_unit.empty() ? "Math" : third_unit, .Unit = third_unit}};
}

/**
 * @class   PanelBuilder
 * @brief   Fills an exported panel, adding each Y axis the first time a trace is plotted on it.
 * @details The axes keep the order in which traces first need them, so the first one, drawn on the left, is the
 *          voltage axis whenever a voltage is shown.
 */
class PanelBuilder {
public:
    PanelBuilder(std::string title, ExportAxes axes) : m_Axes(std::move(axes)) { m_Panel.Title = std::move(title); }

    std::size_t UseAxis(const ImAxis axis) {
        const auto found = std::ranges::find(m_UsedAxes, axis);
        if (found != m_UsedAxes.end()) {
            return static_cast<std::size_t>(found - m_UsedAxes.begin());
        }
        m_UsedAxes.push_back(axis);
        m_Panel.YAxes.push_back(axis == ImAxis_Y1 ? m_Axes.Voltage : axis == ImAxis_Y2 ? m_Axes.Current : m_Axes.Third);
        return m_UsedAxes.size() - 1;
    }

    void Add(ExportSeries series, const ImAxis axis) {
        series.Axis = UseAxis(axis);
        m_Panel.Series.push_back(std::move(series));
    }

    void AddNote(ExportNote note, const ImAxis axis) {
        note.Axis = UseAxis(axis);
        m_Panel.Notes.push_back(std::move(note));
    }

    ExportPanel Take() { return std::move(m_Panel); }

private:
    ExportPanel m_Panel;
    ExportAxes m_Axes;
    std::vector<ImAxis> m_UsedAxes;
};

// Adds the measured voltages and currents of a result, as the plots draw them. The suffix tells the curves of a
// DC sweep family apart in the CSV headers; only the first curve of a family has a legend entry
void AddExportTraces(PanelBuilder &builder, const std::vector<std::vector<double>> &node_values,
                     const std::vector<Core::ComponentTrace> &currents, Schematic &schematic, const std::string &suffix,
                     const bool in_legend, const std::vector<int> &excluded = {}) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
            std::ranges::contains(excluded, static_cast<int>(node))) {
            continue;
        }
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), static_cast<int>(node));
        builder.Add({.Name = label + suffix,
                     .LegendLabel = in_legend ? label : "",
                     .Values = node_values[node],
                     .Color = ToExportColor(GetNodeColor(static_cast<int>(node)))},
                    ImAxis_Y1);
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (!schematic.IsCurrentMeasured(currents[index].Name)) {
            continue;
        }
        const std::string label = std::format("I({})", currents[index].Name);
        builder.Add({.Name = label + suffix,
                     .LegendLabel = in_legend ? label : "",
                     .Values = currents[index].Values,
                     .Color = ToExportColor(GetCurrentColor(index)),
                     .Dashed = true},
                    ImAxis_Y2);
    }
}

// A note for each cell that is wide enough for its label in what the image shows, which has no pixels to measure it
// with, so a share of the span stands for the width of the box
void AddBitNotes(ExportPanel &panel, const Core::Transient &transient, const std::vector<LogicNode> &lanes,
                 const PlotSpan &view) {
    std::vector<LogicSignal> signals;
    for (const LogicNode &node : lanes) {
        signals.push_back({transient.NodeVoltages[static_cast<std::size_t>(node.Node)], node.LogicLevel});
    }
    const auto cells = FindBitCells(transient.Times, signals, view.From, view.To);
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const double middle = static_cast<double>(lanes.size() - 1 - lane) * LaneStride + LaneHighOffset / 2.0;
        for (const LogicSegment &cell : cells[lane]) {
            const double from = std::max(cell.From, view.From);
            const double to = std::min(cell.To, view.To);
            if (to - from >= (view.To - view.From) * MinBitLabelShare) {
                panel.Notes.push_back(
                    {.X = (from + to) / 2.0, .Y = middle, .Text = GetLogicStateName(cell.State), .Centered = true});
            }
        }
    }
}

// The lanes as a panel of their own: each trace is drawn at the height of its lane, and its axis names the lanes,
// while the CSV lists the voltages. The first lane is on top, as in the plot
ExportPanel BuildLanePanel(const Core::Transient &transient, const std::vector<LogicNode> &lanes, Schematic &schematic,
                           const bool bit_values, const PlotSpan &view) {
    ExportPanel panel;
    ExportAxis axis{.Name = "", .Unit = "V", .UsesPrefixes = false};
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const std::size_t position = lanes.size() - 1 - lane;
        const std::string label = GetVoltageLabel(schematic.GetConnectivity(), lanes[lane].Node);
        const std::vector<double> &voltages = transient.NodeVoltages[static_cast<std::size_t>(lanes[lane].Node)];
        axis.Ticks.push_back(
            {.Value = static_cast<double>(position) * LaneStride + LaneHighOffset / 2.0, .Label = label});
        panel.Series.push_back({.Name = label,
                                .Values = BuildLane(voltages, lanes[lane].LogicLevel, position),
                                .Samples = voltages,
                                .Color = ToExportColor(GetNodeColor(lanes[lane].Node))});
    }
    if (bit_values) {
        AddBitNotes(panel, transient, lanes, view);
    }
    axis.Range = ExportRange{.From = LaneLowOffset - LaneMargin,
                             .To = static_cast<double>(lanes.size() - 1) * LaneStride + LaneHighOffset + LaneMargin};
    panel.YAxes.push_back(std::move(axis));
    return panel;
}

void AddExportMath(PanelBuilder &builder, const std::vector<MathTrace> &math, const std::string &suffix,
                   const bool in_legend) {
    for (const MathTrace &trace : math) {
        builder.Add({.Name = trace.Label + suffix,
                     .LegendLabel = in_legend ? trace.Label : "",
                     .Values = trace.Values,
                     .Color = ToExportColor(trace.Color),
                     .Heavy = true},
                    trace.Axis);
    }
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

// The range a logarithmic axis fits to: the positive values of the traces, with a tenth of their decades, and at
// least half a decade, of room above and below. Values far below the largest, such as the share of the ideal
// op-amp, are left below the axis, so they do not squash the plot
PlotSpan GetDecadeFitRange(const std::vector<std::span<const double>> &traces) {
    double highest = 0.0;
    for (const std::span<const double> trace : traces) {
        for (const double value : trace) {
            highest = std::max(highest, value);
        }
    }
    if (highest <= 0.0) {
        return {.From = 1e-12, .To = 1.0};
    }
    double lowest = highest;
    for (const std::span<const double> trace : traces) {
        for (const double value : trace) {
            if (value > highest * DecadeFitFloor) {
                lowest = std::min(lowest, value);
            }
        }
    }
    const double decades = std::log10(highest / lowest);
    const double padding = std::max(decades * 0.1, 0.5);
    return {.From = lowest / std::pow(10.0, padding), .To = highest * std::pow(10.0, padding)};
}

// Densities per root hertz, as noise is written: "V/√Hz" for volts
std::string GetDensityUnit(const std::string &unit) {
    return unit + "/√Hz";
}

std::string FormatFrequencyRange(const Core::Noise &noise) {
    return std::format("{}Hz to {}Hz", Core::FormatValue(noise.Frequencies.front()),
                       Core::FormatValue(noise.Frequencies.back()));
}

std::string GetExtension(const ExportFormat format) {
    return format == ExportFormat::SVG ? ".svg" : ".csv";
}

// Decibels relative to 1 of the unit of the trace, as spectrum analyzers write dBV
std::string GetDecibelUnit(const SpectrumTrace &trace) {
    return trace.Unit.empty() ? " dB" : std::format(" dB{}", trace.Unit);
}

ExportAxes GetSpectrumExportAxes() {
    return {.Voltage = {.Name = "Voltage (dBV)", .Unit = "dBV", .UsesPrefixes = false},
            .Current = {.Name = "Current (dBA)", .Unit = "dBA", .UsesPrefixes = false},
            .Third = {.Name = "Math (dB)", .Unit = "dB", .UsesPrefixes = false}};
}

// Distortion reads as a percentage with three significant digits, down to the floor the interpolation of the
// transient leaves; the decibels give the exact figure below it
std::string FormatDistortionPercent(const double ratio) {
    const double percent = ratio * 100.0;
    return percent < MinShownDistortionPercent ? std::format("<{}%", MinShownDistortionPercent)
                                               : std::format("{:.3g}%", percent);
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
    ProcessExportDialog();
    DrawExportError();
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
    const std::optional<PlotTab> tab_to_show = std::exchange(m_TabToShow, std::nullopt);
    const auto tab_flags = [&tab_to_show](const PlotTab tab) {
        return tab_to_show == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    };
    if (ImGui::BeginTabItem("Operating point", nullptr, tab_flags(PlotTab::OperatingPoint))) {
        m_ShownTab = PlotTab::OperatingPoint;
        if (const auto &operating_point = m_Schematic.GetOperatingPoint()) {
            DrawOperatingPoint(*operating_point, m_Schematic);
        } else {
            ImGui::TextDisabled("No operating point for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Transient", nullptr, tab_flags(PlotTab::Transient))) {
        m_ShownTab = PlotTab::Transient;
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
            ImGui::TextDisabled("No transient for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Spectrum", nullptr, tab_flags(PlotTab::Spectrum))) {
        m_ShownTab = PlotTab::Spectrum;
        if (transient) {
            UpdateSpectra(*transient, transient_math);
            DrawWithStatistics(
                [&] {
                    if (!anything_measured && transient_math.empty()) {
                        ImGui::TextDisabled("%s", nothing_measured);
                    }
                    DrawMathErrors(transient_errors);
                    DrawSpectrum();
                },
                [&] { DrawSpectrumStatistics(); });
        } else {
            ImGui::TextDisabled("No transient for the current circuit; the spectrum transforms its measured traces");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep", nullptr, tab_flags(PlotTab::ACSweep))) {
        m_ShownTab = PlotTab::ACSweep;
        if (sweep) {
            DrawWithStatistics(
                [&] {
                    ImGui::TextDisabled(
                        "%s", anything_measured
                                  ? "Relative to the AC magnitude of the sources: a magnitude of 1 reads as gain"
                                  : nothing_measured);
                    if (!m_MathChannels.empty()) {
                        ImGui::TextDisabled("Math channels are not calculated for AC sweeps yet");
                    }
                    DrawACSweep(*sweep);
                },
                [&] { DrawACSweepStatistics(*sweep); });
        } else {
            ImGui::TextDisabled("No AC sweep for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("DC sweep", nullptr, tab_flags(PlotTab::DCSweep))) {
        m_ShownTab = PlotTab::DCSweep;
        if (dc_sweep) {
            if (!anything_measured && (dc_sweep_math.empty() || dc_sweep_math.front().empty())) {
                ImGui::TextDisabled("%s", nothing_measured);
            }
            DrawMathErrors(dc_sweep_errors);
            DrawDCSweep(*dc_sweep, dc_sweep_math);
        } else {
            ImGui::TextDisabled("No DC sweep for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Noise", nullptr, tab_flags(PlotTab::Noise))) {
        m_ShownTab = PlotTab::Noise;
        if (const auto &noise = m_Schematic.GetNoise()) {
            DrawWithStatistics([&] { DrawNoise(*noise); }, [&] { DrawNoiseStatistics(*noise); });
        } else {
            ImGui::TextDisabled("No noise analysis for the current circuit; pick it beside Run in the editor");
        }
        ImGui::EndTabItem();
    }
    ImPlot::PopStyleVar();

    ImGui::EndTabBar();
    // A plain button apart from the tabs, so it does not read as another page
    ImGui::SetCursorScreenPos({tab_row.x + tab_row_width - export_width, tab_row.y});
    if (ImGui::Button(ExportLabel)) {
        ImGui::OpenPopup(ExportPopup);
    }
    ImGui::SetItemTooltip("Save the plot of this tab as an SVG image or a CSV table");
    // The popup hangs below the button, aligned to its right edge, so it stays inside the window
    const ImVec2 export_corner = {ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y};
    ImGui::SetNextWindowPos(export_corner, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    bool can_export = false;
    switch (m_ShownTab) {
    case PlotTab::OperatingPoint:
        break;
    case PlotTab::Transient:
        can_export = transient && (anything_measured || !transient_math.empty());
        break;
    case PlotTab::Spectrum:
        can_export = transient && m_Spectra && std::ranges::any_of(m_Spectra->Traces, [](const SpectrumTrace &trace) {
                         return trace.Decibels.has_value();
                     });
        break;
    case PlotTab::ACSweep:
        can_export = sweep && anything_measured;
        break;
    case PlotTab::DCSweep:
        can_export = dc_sweep && (anything_measured || (!dc_sweep_math.empty() && !dc_sweep_math.front().empty()));
        break;
    case PlotTab::Noise:
        can_export = m_Schematic.GetNoise().has_value();
        break;
    }
    if (const std::optional<ExportFormat> format = DrawExportPopup(can_export)) {
        if (m_ShownTab == PlotTab::Transient) {
            StartExport(BuildTransientFigure(*transient, transient_math), *format);
        } else if (m_ShownTab == PlotTab::Spectrum) {
            StartExport(BuildSpectrumFigure(), *format);
        } else if (m_ShownTab == PlotTab::ACSweep) {
            StartExport(BuildACSweepFigure(*sweep), *format);
        } else if (m_ShownTab == PlotTab::Noise) {
            StartExport(BuildNoiseFigure(*m_Schematic.GetNoise()), *format);
        } else {
            StartExport(BuildDCSweepFigure(*dc_sweep, dc_sweep_math), *format);
        }
    }
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
    DrawMathChannels(operands);
    ImGui::EndChild();
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
    const bool currents_changed = TakeTracesChanged(m_TransientShownCurrents, std::move(measured_currents));
    const bool voltages_changed = TakeTracesChanged(m_TransientShownNodes, std::move(analog_nodes));
    const bool new_result = TakeFit(m_FittedTransient, m_Schematic.GetTransientVersion());
    SetNextFits(new_result, voltages_changed, currents_changed);
    if (TakeTracesChanged(m_TransientShownMath, ListMathLabels(math))) {
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
    std::vector<PlottedTrace> plotted;
    PlotNodes(transient.Times, transient.NodeVoltages, m_Schematic, "V", "", plotted, lane_nodes);
    if (show_currents) {
        PlotCurrents(transient.Times, transient.Currents, m_Schematic, "A", "", plotted);
    }
    PlotMath(transient.Times, math, "", plotted);
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
    if (BeginTracePlot("Magnitude##ac", ImVec2(-1.0f, height))) {
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
    if (BeginTracePlot("Phase##ac", ImVec2(-1.0f, height))) {
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
    // The padding of the other plots is linear, which would reach below zero on these logarithmic axes, so the fits
    // are worked out in decades: every axis for a new result, and the output axis when the plotted shares change
    const bool new_result = TakeFit(m_FittedNoise, m_Schematic.GetNoiseVersion());
    const bool shares_changed =
        TakeTracesChanged(m_PlottedNoiseContributions,
                          std::vector<std::string>(m_ShownNoiseContributions.begin(), m_ShownNoiseContributions.end()));
    // Referred to a voltage source, the input noise is in volts too and shares the axis of the output
    const bool input_in_volts = noise.InputUnit == "V";
    const ImAxis input_axis = input_in_volts ? ImAxis_Y1 : ImAxis_Y2;
    std::vector<std::span<const double>> voltage_traces{noise.OutputDensities};
    if (input_in_volts) {
        voltage_traces.emplace_back(noise.InputDensities);
    }
    for (const Core::NoiseContribution &contribution : noise.Contributions) {
        if (m_ShownNoiseContributions.contains(contribution.Name)) {
            voltage_traces.emplace_back(contribution.OutputDensities);
        }
    }
    if (!BeginTracePlot("##noise", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    const std::string output_unit = GetDensityUnit("V");
    const std::string input_unit = GetDensityUnit(noise.InputUnit);
    ImPlot::SetupAxes("Frequency", "Noise density", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>(output_unit.c_str()));
    if (!input_in_volts) {
        ImPlot::SetupAxis(ImAxis_Y2, "Input-referred noise", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisScale(ImAxis_Y2, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>(input_unit.c_str()));
    }
    // Set during the setup, since ImPlot fits an axis on its own the first frame it is shown
    if (new_result) {
        ImPlot::SetupAxisLimits(ImAxis_X1, noise.Frequencies.front(), noise.Frequencies.back(), ImPlotCond_Always);
        if (!input_in_volts) {
            const PlotSpan input_range = GetDecadeFitRange({noise.InputDensities});
            ImPlot::SetupAxisLimits(ImAxis_Y2, input_range.From, input_range.To, ImPlotCond_Always);
        }
    }
    if (new_result || shares_changed) {
        const PlotSpan voltage_range = GetDecadeFitRange(voltage_traces);
        ImPlot::SetupAxisLimits(ImAxis_Y1, voltage_range.From, voltage_range.To, ImPlotCond_Always);
    }

    std::vector<PlottedTrace> plotted;
    const std::string output_label = GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode);
    const ImU32 output_color = GetNodeColor(noise.OutputNode);
    ImPlotSpec output_spec;
    output_spec.LineColor = ImGui::ColorConvertU32ToFloat4(output_color);
    output_spec.LineWeight = MathLineWeight;
    ImPlot::PlotLine(output_label.c_str(), noise.Frequencies.data(), noise.OutputDensities.data(),
                     static_cast<int>(noise.Frequencies.size()), output_spec);
    plotted.push_back({output_label, &noise.OutputDensities, output_unit.c_str(), output_color});
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (!m_ShownNoiseContributions.contains(contribution.Name)) {
            continue;
        }
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
        ImPlot::PlotLine(contribution.Name.c_str(), noise.Frequencies.data(), contribution.OutputDensities.data(),
                         static_cast<int>(noise.Frequencies.size()), spec);
        plotted.push_back(
            {contribution.Name, &contribution.OutputDensities, output_unit.c_str(), GetCurrentColor(index)});
    }
    const std::string input_label = std::format("Input-referred ({})", noise.InputSource);
    const ImU32 input_color = AdaptToBackground(InputNoiseColor);
    ImPlot::SetAxes(ImAxis_X1, input_axis);
    PlotDashedLine(input_label, noise.Frequencies, noise.InputDensities, input_color);
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    plotted.push_back({input_label, &noise.InputDensities, input_unit.c_str(), input_color});
    DrawCursorReadout(noise.Frequencies, "Hz", true, plotted);
    const ImPlotRange view = ImPlot::GetPlotLimits().X;
    m_NoiseView = {.From = view.Min, .To = view.Max};
    ImPlot::EndPlot();
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
    const TransientSpectra &spectra = *m_Spectra;
    if (spectra.Traces.empty()) {
        ImGui::TextDisabled("Measure a trace to see its harmonics");
        return;
    }
    if (spectra.Frequencies.size() > 1) {
        ImGui::TextDisabled("%s",
                            std::format("Resolution {}Hz", Core::FormatValue(spectra.Frequencies.front())).c_str());
    }
    // Every trace shares the span and the window, so one warning covers them all
    const auto short_trace = std::ranges::find_if(
        spectra.Traces, [](const SpectrumTrace &trace) { return trace.Harmonics.PeriodsNeeded.has_value(); });
    if (short_trace != spectra.Traces.end()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetWarningTextColor(), "%s",
                           std::format("The stretch needs {} periods to tell the harmonics apart: widen it, or pick "
                                       "the Rectangular window over whole periods",
                                       *short_trace->Harmonics.PeriodsNeeded)
                               .c_str());
        ImGui::PopTextWrapPos();
    }
    for (const SpectrumTrace &trace : spectra.Traces) {
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(trace.Color), "%s", trace.Label.c_str());
        if (!trace.Decibels) {
            ImGui::TextDisabled("Undefined in the stretch");
            continue;
        }
        if (!BeginStatisticsTable(trace.Label)) {
            continue;
        }
        const HarmonicAnalysis &analysis = trace.Harmonics;
        const char *unit = trace.Unit.c_str();
        DrawMeasuredValue("Mean", analysis.Mean, trace.LargestAmplitude, unit);
        if (analysis.Harmonics.empty()) {
            DrawStatistic("Freq", "-");
            ImGui::EndTable();
            continue;
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
        DrawStatistic(input_name.c_str(), FormatQuantity(noise.InputTotal, noise.InputUnit.c_str()));
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
        ImGui::PushStyleColor(ImGuiCol_CheckMark, AdaptToBackground(MathColors[index % MathColors.size()]));
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
                          .Color = AdaptToBackground(MathColors[index % MathColors.size()])});
    }
    return traces;
}

// Like the export dialog of a scope: the image follows the plot as shown, at the size and theme chosen here.
// Returns the format to save once the user picks one
std::optional<ExportFormat> OutputWindow::DrawExportPopup(const bool can_export) {
    if (!ImGui::BeginPopup(ExportPopup)) {
        return std::nullopt;
    }
    ImGui::TextUnformatted(std::format("Export the {} plot", GetTabTitle(m_ShownTab)).c_str());
    ImGui::TextDisabled("The measured traces: the image shows the visible range, the table every sample");
    ImGui::Separator();
    if (ImGui::RadioButton("Light, for print", !m_ExportStyle.Dark)) {
        m_ExportStyle.Dark = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Dark", m_ExportStyle.Dark)) {
        m_ExportStyle.Dark = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * ExportSizeWidth);
    ImGui::InputInt("##width", &m_ExportStyle.Width, 100, 500);
    ImGui::SameLine();
    ImGui::TextUnformatted("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * ExportSizeWidth);
    ImGui::InputInt("pixels##height", &m_ExportStyle.Height, 100, 500);
    m_ExportStyle.Width = std::clamp(m_ExportStyle.Width, MinExportSize, MaxExportSize);
    m_ExportStyle.Height = std::clamp(m_ExportStyle.Height, MinExportSize, MaxExportSize);
    ImGui::Separator();

    std::optional<ExportFormat> format;
    if (!can_export) {
        ImGui::TextDisabled("Run the analysis and measure a trace to export it");
    }
    ImGui::BeginDisabled(!can_export || m_ExportDialog.IsPending());
    if (PrimaryButton("Save SVG...")) {
        format = ExportFormat::SVG;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save CSV...")) {
        format = ExportFormat::CSV;
    }
    ImGui::EndDisabled();
    if (format) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return format;
}

// The image shows the X range the plot shows, so zooming in before exporting crops the image as well
ExportFigure OutputWindow::BuildTransientFigure(const Core::Transient &transient,
                                                const std::vector<MathTrace> &math) const {
    ExportFigure figure;
    figure.XAxis = {.Name = "Time", .Unit = "s"};
    figure.Xs = transient.Times;
    figure.XRange = ExportRange{.From = m_TransientView.From, .To = m_TransientView.To};
    const std::vector<LogicNode> lanes = FindMeasuredLanes(transient.NodeVoltages.size());
    PanelBuilder builder("", GetLinearExportAxes(ThirdAxisUnit(math)));
    AddExportTraces(builder, transient.NodeVoltages, transient.Currents, m_Schematic, "", true, ListNodes(lanes));
    AddExportMath(builder, math, "", true);
    ExportPanel analog = builder.Take();
    // As on screen, the analog plot is left out when only logic signals are measured
    if (!analog.Series.empty() || lanes.empty()) {
        figure.Panels.push_back(std::move(analog));
    }
    if (!lanes.empty()) {
        figure.Panels.push_back(BuildLanePanel(transient, lanes, m_Schematic, m_ShowBitValues, m_TransientView));
    }
    return figure;
}

// The spectra as plotted, over the frequencies the plot shows
ExportFigure OutputWindow::BuildSpectrumFigure() const {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = m_SpectrumLogarithmic;
    figure.Xs = m_Spectra->Frequencies;
    figure.XRange = ExportRange{.From = m_SpectrumView.From, .To = m_SpectrumView.To};
    PanelBuilder builder("", GetSpectrumExportAxes());
    for (const SpectrumTrace &trace : m_Spectra->Traces) {
        if (trace.Decibels) {
            builder.Add({.Name = trace.Label,
                         .LegendLabel = trace.Label,
                         .Values = *trace.Decibels,
                         .Color = ToExportColor(trace.Color),
                         .Dashed = trace.Dashed},
                        trace.Axis);
        }
    }
    figure.Panels.push_back(builder.Take());
    return figure;
}

// Both panels list the same traces, as the two Bode plots do
ExportFigure OutputWindow::BuildACSweepFigure(const Core::ACSweep &sweep) const {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = sweep.Frequencies;
    figure.XRange = ExportRange{.From = m_ACSweepView.From, .To = m_ACSweepView.To};
    PanelBuilder magnitude("Magnitude", {.Voltage = {.Name = "Voltage (dB)", .Unit = "dB", .UsesPrefixes = false},
                                         .Current = {.Name = "Current (dB)", .Unit = "dB", .UsesPrefixes = false},
                                         .Third = {}});
    AddExportTraces(magnitude, sweep.NodeMagnitudesDecibels, sweep.CurrentMagnitudesDecibels, m_Schematic, "", true);
    PanelBuilder phase("Phase", {.Voltage = {.Name = "Phase (deg)", .Unit = "deg", .UsesPrefixes = false},
                                 .Current = {.Name = "Current phase (deg)", .Unit = "deg", .UsesPrefixes = false},
                                 .Third = {}});
    AddExportTraces(phase, sweep.NodePhasesDegrees, sweep.CurrentPhasesDegrees, m_Schematic, "", true);
    figure.Panels.push_back(magnitude.Take());
    figure.Panels.push_back(phase.Take());
    return figure;
}

// A stepped sweep exports every curve, labeled with its step where the curves part most, as in the plot
ExportFigure OutputWindow::BuildDCSweepFigure(const Core::DCSweep &sweep,
                                              const std::vector<std::vector<MathTrace>> &math) const {
    static const std::vector<MathTrace> no_math;
    ExportFigure figure;
    figure.XAxis = {.Name = sweep.SweptSource, .Unit = sweep.SweptUnit};
    figure.Xs = sweep.SweptValues;
    figure.XRange = ExportRange{.From = m_DCSweepView.From, .To = m_DCSweepView.To};
    const bool stepped = !sweep.SteppedSource.empty();
    PanelBuilder builder("", GetLinearExportAxes(ThirdAxisUnit(math.empty() ? no_math : math.front())));
    for (std::size_t curve_index = 0; curve_index < sweep.Curves.size(); ++curve_index) {
        const Core::DCSweepCurve &curve = sweep.Curves[curve_index];
        const std::string suffix = stepped ? " at " + FormatStepLabel(sweep, curve) : "";
        AddExportTraces(builder, curve.NodeVoltages, curve.Currents, m_Schematic, suffix, curve_index == 0);
        if (curve_index < math.size()) {
            AddExportMath(builder, math[curve_index], suffix, curve_index == 0);
        }
    }
    if (stepped) {
        const bool show_currents = HasMeasuredCurrent(sweep.Curves.front().Currents, m_Schematic);
        for (const CurveFamily &family : ListCurveFamilies(sweep, m_Schematic, show_currents, math)) {
            const std::size_t sample = FindWidestSpread(family.Curves);
            if (sample >= sweep.SweptValues.size()) {
                continue;
            }
            for (std::size_t curve = 0; curve < family.Curves.size(); ++curve) {
                const double y = (*family.Curves[curve])[sample];
                if (std::isfinite(y)) {
                    builder.AddNote(
                        {.X = sweep.SweptValues[sample], .Y = y, .Text = FormatStepLabel(sweep, sweep.Curves[curve])},
                        family.Axis);
                }
            }
        }
    }
    figure.Panels.push_back(builder.Take());
    return figure;
}

// The densities on logarithmic axes, as plotted, with the shares of the parts checked in the panel
ExportFigure OutputWindow::BuildNoiseFigure(const Core::Noise &noise) const {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = noise.Frequencies;
    figure.XRange = ExportRange{.From = m_NoiseView.From, .To = m_NoiseView.To};
    ExportPanel panel;
    panel.YAxes = {{.Name = "Noise density", .Unit = GetDensityUnit("V"), .Logarithmic = true}};
    const bool input_in_volts = noise.InputUnit == "V";
    if (!input_in_volts) {
        panel.YAxes.push_back(
            {.Name = "Input-referred noise", .Unit = GetDensityUnit(noise.InputUnit), .Logarithmic = true});
    }
    const std::string output_label = GetVoltageLabel(m_Schematic.GetConnectivity(), noise.OutputNode);
    panel.Series.push_back({.Name = output_label,
                            .LegendLabel = output_label,
                            .Values = noise.OutputDensities,
                            .Color = ToExportColor(GetNodeColor(noise.OutputNode)),
                            .Heavy = true});
    for (std::size_t index = 0; index < noise.Contributions.size(); ++index) {
        const Core::NoiseContribution &contribution = noise.Contributions[index];
        if (m_ShownNoiseContributions.contains(contribution.Name)) {
            panel.Series.push_back({.Name = contribution.Name,
                                    .LegendLabel = contribution.Name,
                                    .Values = contribution.OutputDensities,
                                    .Color = ToExportColor(GetCurrentColor(index))});
        }
    }
    const std::string input_label = std::format("Input-referred ({})", noise.InputSource);
    panel.Series.push_back({.Name = input_label,
                            .LegendLabel = input_label,
                            .Values = noise.InputDensities,
                            .Axis = input_in_volts ? 0U : 1U,
                            .Color = ToExportColor(AdaptToBackground(InputNoiseColor)),
                            .Dashed = true});
    figure.Panels.push_back(std::move(panel));
    return figure;
}

// The figure is captured now, so the file holds what the plot showed when the user asked, whatever happens while
// the dialog is open. The dialog starts next to the schematic, with a name after it and the tab
void OutputWindow::StartExport(ExportFigure figure, const ExportFormat format) {
    if (m_ExportDialog.IsPending()) {
        return;
    }
    m_PendingFigure = std::move(figure);
    m_PendingFormat = format;
    const auto &schematic_path = m_Schematic.GetFilePath();
    const std::string stem = schematic_path ? schematic_path->stem().string() : "circuit";
    const std::string file_name = std::format("{}-{}{}", stem, GetTabFileSuffix(m_ShownTab), GetExtension(format));
    const std::filesystem::path location =
        schematic_path ? schematic_path->parent_path() / file_name : std::filesystem::path(file_name);
    if (format == ExportFormat::SVG) {
        m_ExportDialog.ShowSave(SVGFilters, location.string());
    } else {
        m_ExportDialog.ShowSave(CSVFilters, location.string());
    }
}

// Dialogs do not always add the extension, so it is added here when missing
void OutputWindow::ProcessExportDialog() {
    const std::optional<std::optional<std::filesystem::path>> result = m_ExportDialog.TakeResult();
    if (!result) {
        return;
    }
    const std::optional<ExportFigure> figure = std::exchange(m_PendingFigure, std::nullopt);
    if (!*result || !figure) {
        return;
    }
    std::filesystem::path path = **result;
    const std::string extension = GetExtension(m_PendingFormat);
    if (path.extension() != extension) {
        path += extension;
    }
    std::expected<std::string, std::string> text =
        m_PendingFormat == ExportFormat::SVG ? RenderSvg(*figure, m_ExportStyle) : RenderCsv(*figure);
    std::expected<void, std::string> written =
        text ? WriteTextFile(path, *text) : std::expected<void, std::string>(std::unexpected(text.error()));
    if (!written) {
        m_ExportError = written.error();
        ImGui::OpenPopup(ExportErrorPopup);
    }
}

void OutputWindow::DrawExportError() {
    if (!ImGui::BeginPopupModal(ExportErrorPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextColored(GetErrorTextColor(), "%s", m_ExportError.c_str());
    if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace GUI
