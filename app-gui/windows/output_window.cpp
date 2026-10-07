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
constexpr ImVec2 InitialSize = {50.0f, 35.0f};
// ImPlot adds half of it on each side, so 0.4 leaves 20% of the data range above and below the curves
constexpr ImVec2 FitPadding = {0.0f, 0.4f};
// Current dashes, in font sizes
constexpr float DashLength = 0.5f;
constexpr float DashGap = 0.3f;
constexpr float DashWeight = 1.5f;
constexpr ImU32 CursorLineColor = IM_COL32(255, 255, 255, 90);

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
    ImDrawList *draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const LineSegment &dash : SplitIntoDashes(points, DashLength * font_size, DashGap * font_size)) {
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

// Returns whether the current axis has just appeared: it did not exist when the axes last fitted, so its range
// is meaningless until it is fitted too
bool TakeCurrentAxisAppeared(bool &showed_currents, const bool show_currents) {
    return show_currents && !std::exchange(showed_currents, show_currents);
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
            if (!anything_measured) {
                ImGui::TextDisabled("%s", nothing_measured);
            }
            DrawTransient(*transient);
        } else {
            ImGui::TextDisabled("No transient for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        if (sweep) {
            ImGui::TextDisabled("%s", anything_measured
                                          ? "Relative to the AC sources: an amplitude of 1 V reads as gain"
                                          : nothing_measured);
            DrawACSweep(*sweep);
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
    const bool show_currents = HasMeasuredCurrent(transient.Currents, m_Schematic);
    const bool current_axis_appeared = TakeCurrentAxisAppeared(m_TransientShowedCurrents, show_currents);
    if (TakeFit(m_FittedTransient, m_Schematic.GetTransientVersion())) {
        ImPlot::SetNextAxesToFit();
    } else if (current_axis_appeared) {
        ImPlot::SetNextAxisToFit(ImAxis_Y2);
    }
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
    DrawCursorReadout(transient.Times, "s", false, plotted);
    ImPlot::EndPlot();
}

// Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
// their phases follow so each current stays on the same side in both plots
void OutputWindow::DrawACSweep(const Core::ACSweep &sweep) {
    const bool fit = TakeFit(m_FittedACSweep, m_Schematic.GetACSweepVersion());
    const bool show_currents = HasMeasuredCurrent(sweep.CurrentMagnitudesDecibels, m_Schematic);
    const bool current_axis_appeared = TakeCurrentAxisAppeared(m_ACSweepShowedCurrents, show_currents);
    const float height = PlotHeight(2);
    if (fit) {
        ImPlot::SetNextAxesToFit();
    } else if (current_axis_appeared) {
        ImPlot::SetNextAxisToFit(ImAxis_Y2);
    }
    if (ImPlot::BeginPlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Voltage (dB)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current (dB)", ImPlotAxisFlags_AuxDefault);
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentMagnitudesDecibels, m_Schematic, " dB", "", plotted);
        }
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
    if (fit) {
        ImPlot::SetNextAxesToFit();
    } else if (current_axis_appeared) {
        ImPlot::SetNextAxisToFit(ImAxis_Y2);
    }
    if (ImPlot::BeginPlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        if (show_currents) {
            ImPlot::SetupAxis(ImAxis_Y2, "Current phase (deg)", ImPlotAxisFlags_AuxDefault);
        }
        std::vector<PlottedTrace> plotted;
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees, m_Schematic, " deg", "", plotted);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentPhasesDegrees, m_Schematic, " deg", "", plotted);
        }
        DrawCursorReadout(sweep.Frequencies, "Hz", true, plotted);
        ImPlot::EndPlot();
    }
}

// Every curve of a family plots under the same label, so a trace keeps one color and one legend entry
void OutputWindow::DrawDCSweep(const Core::DCSweep &sweep) {
    const Core::DCSweepCurve &first_curve = sweep.Curves.front();
    const bool show_currents = HasMeasuredCurrent(first_curve.Currents, m_Schematic);
    const bool current_axis_appeared = TakeCurrentAxisAppeared(m_DCSweepShowedCurrents, show_currents);
    if (TakeFit(m_FittedDCSweep, m_Schematic.GetDCSweepVersion())) {
        ImPlot::SetNextAxesToFit();
    } else if (current_axis_appeared) {
        ImPlot::SetNextAxisToFit(ImAxis_Y2);
    }
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

} // namespace GUI
