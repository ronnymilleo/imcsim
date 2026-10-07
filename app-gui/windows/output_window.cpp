/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient and AC sweep results of the schematic.
 */

#include "output_window.h"

#include "implot.h"
#include "node_colors.h"
#include "spice_value.h"
#include <algorithm>
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

// Ground is always 0 V, so plots start at node 1. Each node keeps its color whichever nodes are hidden
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values,
               const std::set<std::size_t> &hidden_nodes) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        if (hidden_nodes.contains(node)) {
            continue;
        }
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetNodeColor(static_cast<int>(node)));
        const std::string label = std::format("V({})", node);
        ImPlot::PlotLine(label.c_str(), xs.data(), node_values[node].data(), static_cast<int>(xs.size()), spec);
    }
}

// Currents follow the ImPlot colormap, by their position in the circuit, so they stand apart from the node colors
ImU32 GetCurrentColor(const std::size_t index) {
    return ImGui::ColorConvertFloat4ToU32(ImPlot::GetColormapColor(static_cast<int>(index)));
}

bool HasShownCurrent(const std::vector<Core::ComponentTrace> &currents, const std::set<std::string> &hidden_currents) {
    return std::ranges::any_of(currents, [&hidden_currents](const Core::ComponentTrace &current) {
        return !hidden_currents.contains(current.Name);
    });
}

// Currents go on the secondary Y axis, which the caller sets up before plotting anything
void PlotCurrents(const std::vector<double> &xs, const std::vector<Core::ComponentTrace> &currents,
                  const std::set<std::string> &hidden_currents) {
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    for (std::size_t index = 0; index < currents.size(); ++index) {
        if (hidden_currents.contains(currents[index].Name)) {
            continue;
        }
        ImPlotSpec spec;
        spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
        const std::string label = std::format("I({})", currents[index].Name);
        ImPlot::PlotLine(label.c_str(), xs.data(), currents[index].Values.data(), static_cast<int>(xs.size()), spec);
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

// Shows/hides a set of traces with two buttons; the ID keeps the buttons of each section apart
template <typename Key> void DrawAllNoneButtons(const char *id, std::set<Key> &hidden, const std::vector<Key> &keys) {
    ImGui::PushID(id);
    if (ImGui::SmallButton("All")) {
        hidden.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        hidden.insert(keys.begin(), keys.end());
    }
    ImGui::PopID();
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
    // Both results come from the same circuit, so they have the same nodes and components
    const std::size_t node_count =
        std::max(transient ? transient->NodeVoltages.size() : 0, sweep ? sweep->NodeMagnitudesDecibels.size() : 0);
    static const std::vector<Core::ComponentTrace> no_currents;
    const std::vector<Core::ComponentTrace> &currents =
        transient ? transient->Currents : (sweep ? sweep->CurrentMagnitudesDecibels : no_currents);
    if (node_count > 1 || !currents.empty()) {
        DrawTraceList(node_count, currents);
        ImGui::SameLine();
    }

    if (!ImGui::BeginChild("plots") || !ImGui::BeginTabBar("plots")) {
        ImGui::EndChild();
        return;
    }
    // Applies to the automatic fit of every new result and to the user's double-click fit
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, FitPadding);
    if (ImGui::BeginTabItem("Transient")) {
        if (transient) {
            DrawTransient(*transient);
        } else {
            ImGui::TextDisabled("No transient for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        if (sweep) {
            ImGui::TextDisabled("Relative to the AC sources: an amplitude of 1 V reads as gain");
            DrawACSweep(*sweep);
        } else {
            ImGui::TextDisabled("No AC sweep for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
    ImGui::EndChild();
}

// The check marks take the colors of the curves; node colors also match the wires when the editor shows its nodes
void OutputWindow::DrawTraceList(const std::size_t node_count, const std::vector<Core::ComponentTrace> &currents) {
    ImGui::BeginChild("traces", ImVec2(ImGui::GetFontSize() * TraceListWidth, 0.0f), ImGuiChildFlags_Borders);
    if (node_count > 1) {
        ImGui::TextDisabled("Voltages");
        std::vector<std::size_t> nodes;
        for (std::size_t node = 1; node < node_count; ++node) {
            nodes.push_back(node);
        }
        DrawAllNoneButtons("voltages", m_HiddenNodes, nodes);
        for (const std::size_t node : nodes) {
            bool shown = !m_HiddenNodes.contains(node);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetNodeColor(static_cast<int>(node)));
            if (ImGui::Checkbox(std::format("V({})", node).c_str(), &shown)) {
                if (shown) {
                    m_HiddenNodes.erase(node);
                } else {
                    m_HiddenNodes.insert(node);
                }
            }
            ImGui::PopStyleColor();
        }
    }

    if (!currents.empty()) {
        ImGui::TextDisabled("Currents");
        std::vector<std::string> names;
        for (const Core::ComponentTrace &current : currents) {
            names.push_back(current.Name);
        }
        DrawAllNoneButtons("currents", m_HiddenCurrents, names);
        for (std::size_t index = 0; index < names.size(); ++index) {
            bool shown = !m_HiddenCurrents.contains(names[index]);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, GetCurrentColor(index));
            if (ImGui::Checkbox(std::format("I({})", names[index]).c_str(), &shown)) {
                if (shown) {
                    m_HiddenCurrents.erase(names[index]);
                } else {
                    m_HiddenCurrents.insert(names[index]);
                }
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
}

void OutputWindow::DrawTransient(const Core::Transient &transient) {
    const bool show_currents = HasShownCurrent(transient.Currents, m_HiddenCurrents);
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
    PlotNodes(transient.Times, transient.NodeVoltages, m_HiddenNodes);
    if (show_currents) {
        PlotCurrents(transient.Times, transient.Currents, m_HiddenCurrents);
    }
    ImPlot::EndPlot();
}

// Currents go on the secondary axis of both plots: their magnitudes are relative to 1 A instead of 1 V, and
// their phases follow so each current stays on the same side in both plots
void OutputWindow::DrawACSweep(const Core::ACSweep &sweep) {
    const bool fit = TakeFit(m_FittedACSweep, m_Schematic.GetACSweepVersion());
    const bool show_currents = HasShownCurrent(sweep.CurrentMagnitudesDecibels, m_HiddenCurrents);
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
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_HiddenNodes);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentMagnitudesDecibels, m_HiddenCurrents);
        }
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
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees, m_HiddenNodes);
        if (show_currents) {
            PlotCurrents(sweep.Frequencies, sweep.CurrentPhasesDegrees, m_HiddenCurrents);
        }
        ImPlot::EndPlot();
    }
}

} // namespace GUI
