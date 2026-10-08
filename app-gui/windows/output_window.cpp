/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient, AC sweep and DC sweep results of the schematic.
 */

#include "output_window.h"

#include "implot.h"
#include "implot_internal.h"
#include "node_colors.h"
#include "plot_helpers.h"
#include "spice_value.h"
#include "trace_statistics.h"
#include <algorithm>
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
constexpr float TraceListWidth = 7.0f;
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

// Labels the end of every shown curve with the value of the stepped source, so the curves of a family tell apart.
// Currents sit on the secondary Y axis, like the curves they label
void LabelCurveEnds(const Core::DCSweep &sweep, const Schematic &schematic, const bool show_currents) {
    const std::size_t last = sweep.SweptValues.size() - 1;
    const double x = sweep.SweptValues[last];
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    for (const Core::DCSweepCurve &curve : sweep.Curves) {
        const std::string label =
            std::format("{}={}{}", sweep.SteppedSource, Core::FormatValue(curve.StepValue), sweep.SteppedUnit);
        for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
            if (schematic.IsVoltageMeasured(static_cast<int>(node))) {
                ImPlot::Annotation(x, curve.NodeVoltages[node][last], color, ImVec2(4.0f, 0.0f), true, "%s",
                                   label.c_str());
            }
        }
        if (!show_currents) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        for (const Core::ComponentTrace &current : curve.Currents) {
            if (schematic.IsCurrentMeasured(current.Name)) {
                ImPlot::Annotation(x, current.Values[last], color, ImVec2(4.0f, 0.0f), true, "%s", label.c_str());
            }
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
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
                    if (!anything_measured) {
                        ImGui::TextDisabled("%s", nothing_measured);
                    }
                    DrawTransient(*transient);
                },
                [&] { DrawTransientStatistics(*transient); });
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
            if (!anything_measured) {
                ImGui::TextDisabled("%s", nothing_measured);
            }
            DrawDCSweep(*dc_sweep);
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
    ImGui::EndChild();
}

void OutputWindow::DrawTransient(const Core::Transient &transient) {
    std::vector<std::string> measured_currents = ListMeasuredCurrents(transient.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = TakeTracesChanged(m_TransientShownCurrents, std::move(measured_currents));
    const bool voltages_changed =
        TakeTracesChanged(m_TransientShownNodes, ListMeasuredNodes(transient.NodeVoltages.size(), m_Schematic));
    SetNextFits(TakeFit(m_FittedTransient, m_Schematic.GetTransientVersion()), voltages_changed, currents_changed);
    if (!ImPlot::BeginPlot("##transient", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage");
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    std::vector<PlottedTrace> plotted;
    PlotNodes(transient.Times, transient.NodeVoltages, m_Schematic, "V", "", plotted);
    if (show_currents) {
        PlotCurrents(transient.Times, transient.Currents, m_Schematic, "A", "", plotted);
    }
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
    const float height = PlotHeight(2);
    SetNextFits(fit, voltages_changed, currents_changed);
    if (ImPlot::BeginPlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Voltage (dB)");
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
        ImPlot::SetupAxes("Frequency", "Phase (deg)");
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
void OutputWindow::DrawDCSweep(const Core::DCSweep &sweep) {
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(first_curve.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = TakeTracesChanged(m_DCSweepShownCurrents, std::move(measured_currents));
    const bool voltages_changed =
        TakeTracesChanged(m_DCSweepShownNodes, ListMeasuredNodes(first_curve.NodeVoltages.size(), m_Schematic));
    SetNextFits(TakeFit(m_FittedDCSweep, m_Schematic.GetDCSweepVersion()), voltages_changed, currents_changed);
    const bool stepped = !sweep.SteppedSource.empty();
    if (stepped) {
        ImGui::TextDisabled("%s",
                            std::format("One curve per value of {}, labeled at its end", sweep.SteppedSource).c_str());
    }
    if (!ImPlot::BeginPlot("##dc", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    ImPlot::SetupAxes(sweep.SweptSource.c_str(), "Voltage");
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(sweep.SweptUnit.c_str()));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    std::vector<PlottedTrace> plotted;
    for (const Core::DCSweepCurve &curve : sweep.Curves) {
        const std::string suffix = stepped ? std::format(" at {}={}{}", sweep.SteppedSource,
                                                         Core::FormatValue(curve.StepValue), sweep.SteppedUnit)
                                           : "";
        PlotNodes(sweep.SweptValues, curve.NodeVoltages, m_Schematic, "V", suffix, plotted);
        if (show_currents) {
            PlotCurrents(sweep.SweptValues, curve.Currents, m_Schematic, "A", suffix, plotted);
        }
    }
    if (stepped) {
        LabelCurveEnds(sweep, m_Schematic, show_currents);
    }
    DrawCursorReadout(sweep.SweptValues, sweep.SweptUnit.c_str(), false, plotted);
    ImPlot::EndPlot();
}

// Like the measurements of an oscilloscope, for every shown trace; with the cursors, also the value of each
// trace at both and their difference, and the time between them with its frequency
void OutputWindow::DrawTransientStatistics(const Core::Transient &transient) {
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
    const std::vector<PlottedTrace> traces =
        ListTraces(transient.NodeVoltages, transient.Currents, m_Schematic, "V", "A");
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
        if (const auto statistics = ComputeTraceStatistics(transient.Times, *trace.Values, span.From, span.To)) {
            DrawStatistic("Pk-Pk", FormatQuantity(statistics->PeakToPeak, trace.Unit));
            DrawStatistic("Max", FormatQuantity(statistics->Maximum, trace.Unit));
            DrawStatistic("Min", FormatQuantity(statistics->Minimum, trace.Unit));
            DrawStatistic("Mean", FormatQuantity(statistics->Mean, trace.Unit));
            DrawStatistic("RMS", FormatQuantity(statistics->RMS, trace.Unit));
            DrawStatistic("Freq", FormatOptionalQuantity(statistics->Frequency, "Hz"));
            DrawStatistic("Period", statistics->Frequency ? FormatQuantity(1.0 / *statistics->Frequency, "s") : "-");
        }
        if (cursors.Shown) {
            const double at_a = InterpolateAt(transient.Times, *trace.Values, cursors.A, false);
            const double at_b = InterpolateAt(transient.Times, *trace.Values, cursors.B, false);
            DrawStatistic("At A", FormatQuantity(at_a, trace.Unit));
            DrawStatistic("At B", FormatQuantity(at_b, trace.Unit));
            DrawStatistic("B-A", FormatQuantity(at_b - at_a, trace.Unit));
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

} // namespace GUI
