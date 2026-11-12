/**
 * @file    operating_point_tab.cpp
 * @brief   The Operating point tab of the Output window: the DC values of the last operating point, and the measured
 *          traces against the step value of a stepped one.
 */

#include "windows/output_tabs/operating_point_tab.h"

#include "base/theme.h"
#include "imgui.h"
#include "implot.h"
#include "results/parameter_step.h"
#include "spice/spice_value.h"
#include "windows/output_tabs/cursor_widgets.h"
#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/step_widgets.h"
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

/// Height of the plot of a stepped run, in font sizes, so the tables of the step shown stay in view below it.
constexpr float STEP_PLOT_HEIGHT = 16.0f;
/// Radius of the marker at each step, in pixels.
constexpr float STEP_MARKER_SIZE = 3.0f;

/**
 * @brief   Draws the operating point as tables of DC values, one row per node and per current, under its warnings.
 * @param[in] operating_point  The result.
 * @param[in] schematic        The names of nets, which a node shows before its number.
 */
void DrawOperatingPoint(const Core::OperatingPoint &operating_point, Schematic &schematic) {
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : operating_point.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    // Fixed widths, so the currents fit beside the voltages
    const ImVec2 table_size = {ImGui::GetFontSize() * 14.0f, 0.0f};
    if (ImGui::BeginTable("node_voltages", 2, TABLE_FLAGS, table_size)) {
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
            ImGui::TextUnformatted(
                std::format("{}V", Core::FormatMeasurement(operating_point.NodeVoltages[node])).c_str());
        }
        ImGui::EndTable();
    }
    if (operating_point.Currents.empty()) {
        return;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
    if (ImGui::BeginTable("currents", 2, TABLE_FLAGS, table_size)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("Current");
        ImGui::TableHeadersRow();
        for (const Core::ComponentCurrent &current : operating_point.Currents) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(current.Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::format("{}A", Core::FormatMeasurement(current.Current)).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Currents as in SPICE: positive through a two-terminal part from its first terminal to its "
                        "second, and into each terminal of a transistor");
    ImGui::PopTextWrapPos();
}

/**
 * @brief   Marks the value of each trace at every step, the points that were simulated, with no legend entry of its
 * own.
 */
void MarkSteps(const std::vector<double> &xs, const std::span<const PlottedTrace> traces, const ImAxis axis) {
    ImPlot::SetAxes(ImAxis_X1, axis);
    for (const PlottedTrace &trace : traces) {
        ImPlotSpec spec;
        spec.Marker = ImPlotMarker_Circle;
        spec.MarkerSize = STEP_MARKER_SIZE;
        spec.MarkerFillColor = ImGui::ColorConvertU32ToFloat4(trace.Color);
        spec.MarkerLineColor = spec.MarkerFillColor;
        ImPlot::PlotScatter(std::format("##{} steps", trace.Label).c_str(), xs.data(), trace.Values->data(),
                            static_cast<int>(xs.size()), spec);
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

/**
 * @brief   Tells whether two sweeps hold the same values, so picking another step, which lays the same steps out again,
 *          keeps the zoom of the plot.
 */
bool HaveSameValues(const Core::DCSweep &first, const Core::DCSweep &second) {
    if (first.SweptSource != second.SweptSource || first.SweptValues != second.SweptValues ||
        first.Curves.size() != second.Curves.size()) {
        return false;
    }
    for (std::size_t curve = 0; curve < first.Curves.size(); ++curve) {
        const Core::DCSweepCurve &one = first.Curves[curve];
        const Core::DCSweepCurve &other = second.Curves[curve];
        if (one.NodeVoltages != other.NodeVoltages || one.Currents.size() != other.Currents.size()) {
            return false;
        }
        for (std::size_t current = 0; current < one.Currents.size(); ++current) {
            if (one.Currents[current].Name != other.Currents[current].Name ||
                one.Currents[current].Values != other.Currents[current].Values) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

/**
 * @brief   Creates the tab.
 * @param[in] schematic  Schematic that keeps the measurements and the names of nets; it must outlive the tab.
 * @param[in] results    Results whose operating point is shown; they must outlive the tab.
 */
OperatingPointTab::OperatingPointTab(Schematic &schematic, ResultStore &results)
    : m_Schematic(schematic), m_Results(results) {
}

/**
 * @brief   Lays the steps of a run stepped through a parameter out as a sweep, when the result changed; the Output
 * window calls it every frame before it lists the traces, so the steps can be measured from the list.
 */
void OperatingPointTab::Update() {
    const std::size_t version = m_Results.GetOperatingPointVersion();
    if (m_SweptVersion == version) {
        return;
    }
    m_SweptVersion = version;
    const auto &steps = m_Results.GetOperatingPointSteps();
    if (!steps || steps->Parameter == MONTE_CARLO_PARAMETER) {
        m_StepSweep.reset();
        return;
    }
    Core::DCSweep sweep = JoinOperatingPointSteps(*steps);
    if (!m_StepSweep || !HaveSameValues(*m_StepSweep, sweep)) {
        ++m_StepSweepVersion;
    }
    m_StepSweep = std::move(sweep);
}

/**
 * @brief   Draws the tab: the step picker and the plot of a stepped run, then the tables of the step shown, or how to
 *          get an operating point.
 */
void OperatingPointTab::Draw() {
    const auto &operating_point = m_Results.GetOperatingPoint();
    if (!operating_point) {
        ImGui::TextDisabled("No operating point for the current circuit; pick it beside Run in the editor");
        return;
    }
    if (const auto &steps = m_Results.GetOperatingPointSteps()) {
        if (const std::optional<std::size_t> picked =
                DrawStepSelector(steps->Parameter, steps->Unit, steps->Values, steps->Selected)) {
            m_Results.SelectOperatingPointStep(*picked);
        }
        ImGui::SetItemTooltip("The step the schematic and the tables below show");
        if (steps->Parameter == MONTE_CARLO_PARAMETER) {
            ImGui::TextDisabled("How each value spreads over the runs is in the Monte Carlo tab");
        } else if (m_StepSweep) {
            DrawStepPlot(*m_StepSweep);
        }
    }
    // The selector may have picked another step this frame
    if (const auto &shown = m_Results.GetOperatingPoint()) {
        DrawOperatingPoint(*shown, m_Schematic);
    }
}

/**
 * @brief   The steps laid out as a sweep of the parameter, for the trace list.
 * @return  The sweep, or null unless the result is a run stepped through a parameter.
 */
const Core::DCSweep *OperatingPointTab::GetStepSweep() const {
    return m_StepSweep ? &*m_StepSweep : nullptr;
}

/**
 * @brief   Every measured trace against the step value, with a marker at each step, since only those were simulated;
 *          currents go on the secondary axis.
 */
void OperatingPointTab::DrawStepPlot(const Core::DCSweep &sweep) {
    const Core::DCSweepCurve &curve = sweep.Curves.front();
    std::vector<std::string> measured_currents = ListMeasuredCurrents(curve.Currents, m_Schematic);
    const bool show_currents = !measured_currents.empty();
    const bool currents_changed = m_Fit.TakeCurrentsChanged(std::move(measured_currents));
    const bool voltages_changed = m_Fit.TakeNodesChanged(ListMeasuredNodes(curve.NodeVoltages.size(), m_Schematic));
    const bool has_voltages = HasMeasuredVoltage(curve.NodeVoltages.size(), m_Schematic);
    if (!has_voltages && !show_currents) {
        ImGui::TextDisabled("%s", NOTHING_MEASURED);
    }
    SetNextFits(m_Fit.TakeNewResult(m_StepSweepVersion), voltages_changed, currents_changed);
    if (!BeginTracePlot("##operating_point_steps", ImVec2(-1.0f, ImGui::GetFontSize() * STEP_PLOT_HEIGHT))) {
        return;
    }
    ImPlot::SetupAxes(sweep.SweptSource.c_str(), "Voltage", ImPlotAxisFlags_None,
                      VoltageAxisFlags(has_voltages, show_currents));
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisValue, const_cast<char *>(sweep.SweptUnit.c_str()));
    ImPlot::SetupAxisFormat(ImAxis_Y1, FormatAxisValue, const_cast<char *>("V"));
    if (show_currents) {
        ImPlot::SetupAxis(ImAxis_Y2, "Current", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y2, FormatAxisValue, const_cast<char *>("A"));
    }
    std::vector<PlottedTrace> plotted;
    PlotNodes(sweep.SweptValues, curve.NodeVoltages, m_Schematic, "V", "", plotted);
    MarkSteps(sweep.SweptValues, plotted, ImAxis_Y1);
    const std::size_t voltage_count = plotted.size();
    if (show_currents) {
        PlotCurrents(sweep.SweptValues, curve.Currents, m_Schematic, "A", "", plotted);
        MarkSteps(sweep.SweptValues, std::span(plotted).subspan(voltage_count), ImAxis_Y2);
    }
    DrawCursorReadout(sweep.SweptValues, sweep.SweptUnit.c_str(), false, plotted);
    ImPlot::EndPlot();
}

} // namespace GUI
