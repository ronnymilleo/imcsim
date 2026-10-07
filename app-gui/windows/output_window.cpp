/**
 * @file    output_window.cpp
 * @brief   Window that plots the transient and AC sweep results of the schematic.
 */

#include "output_window.h"

#include "implot.h"
#include "spice_value.h"
#include <algorithm>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace GUI {

namespace {

// In font sizes, so the layout follows the DPI scale
constexpr float MinPlotHeight = 12.0f;
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

// Ground is always 0 V, so plots start at node 1
void PlotNodes(const std::vector<double> &xs, const std::vector<std::vector<double>> &node_values) {
    for (std::size_t node = 1; node < node_values.size(); ++node) {
        const std::string label = std::format("V({})", node);
        ImPlot::PlotLine(label.c_str(), xs.data(), node_values[node].data(), static_cast<int>(xs.size()));
    }
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
    const bool fit = m_FittedResults != m_Schematic.GetResultsVersion();
    m_FittedResults = m_Schematic.GetResultsVersion();

    if (!ImGui::BeginTabBar("plots")) {
        return;
    }
    if (ImGui::BeginTabItem("Transient")) {
        if (const auto &transient = m_Schematic.GetTransient()) {
            DrawTransient(*transient, fit);
        } else {
            ImGui::TextDisabled("No transient for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        if (const auto &sweep = m_Schematic.GetACSweep()) {
            ImGui::TextDisabled("Relative to the AC sources: an amplitude of 1 V reads as gain");
            DrawACSweep(*sweep, fit);
        } else {
            ImGui::TextDisabled("No AC sweep for the current circuit; run one in the Simulation window");
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void OutputWindow::DrawTransient(const Core::Transient &transient, const bool fit) {
    if (fit) {
        ImPlot::SetNextAxesToFit();
    }
    if (!ImPlot::BeginPlot("##transient", ImVec2(-1.0f, PlotHeight(1)))) {
        return;
    }
    ImPlot::SetupAxes("Time", "Voltage");
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("s"));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    PlotNodes(transient.Times, transient.NodeVoltages);
    ImPlot::EndPlot();
}

void OutputWindow::DrawACSweep(const Core::ACSweep &sweep, const bool fit) {
    const float height = PlotHeight(2);
    if (fit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Magnitude##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Magnitude (dB)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        PlotNodes(sweep.Frequencies, sweep.NodeMagnitudesDecibels);
        ImPlot::EndPlot();
    }
    if (fit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Phase##ac", ImVec2(-1.0f, height))) {
        ImPlot::SetupAxes("Frequency", "Phase (deg)");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>("Hz"));
        PlotNodes(sweep.Frequencies, sweep.NodePhasesDegrees);
        ImPlot::EndPlot();
    }
}

} // namespace GUI
