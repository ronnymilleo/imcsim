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
constexpr float NodeListWidth = 7.0f;
constexpr ImVec2 InitialSize = {50.0f, 35.0f};

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
    // Both results come from the same circuit, so they normally have the same nodes
    const std::size_t node_count =
        std::max(transient ? transient->NodeVoltages.size() : 0, sweep ? sweep->NodeMagnitudesDecibels.size() : 0);
    if (node_count > 1) {
        DrawNodeList(node_count);
        ImGui::SameLine();
    }

    if (!ImGui::BeginChild("plots") || !ImGui::BeginTabBar("plots")) {
        ImGui::EndChild();
        return;
    }
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
    ImGui::EndTabBar();
    ImGui::EndChild();
}

// The check marks take the node colors, which also match the wires when the editor shows its nodes
void OutputWindow::DrawNodeList(const std::size_t node_count) {
    ImGui::BeginChild("nodes", ImVec2(ImGui::GetFontSize() * NodeListWidth, 0.0f), ImGuiChildFlags_Borders);
    if (ImGui::SmallButton("All")) {
        m_HiddenNodes.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        for (std::size_t node = 1; node < node_count; ++node) {
            m_HiddenNodes.insert(node);
        }
    }
    for (std::size_t node = 1; node < node_count; ++node) {
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
    ImGui::EndChild();
}

void OutputWindow::DrawTransient(const Core::Transient &transient) {
    if (TakeFit(m_FittedTransient, m_Schematic.GetTransientVersion())) {
        ImPlot::SetNextAxesToFit();
    }
    if (!ImPlot::BeginPlot("##transient", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage");
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    PlotNodes(transient.Times, transient.NodeVoltages, m_HiddenNodes);
    ImPlot::EndPlot();
}

void OutputWindow::DrawACSweep(const Core::ACSweep &sweep) {
    const bool fit = TakeFit(m_FittedACSweep, m_Schematic.GetACSweepVersion());
    const float height = PlotHeight(2);
    if (fit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Magnitude (dB)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels, m_HiddenNodes);
        ImPlot::EndPlot();
    }
    if (fit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees, m_HiddenNodes);
        ImPlot::EndPlot();
    }
}

} // namespace GUI
