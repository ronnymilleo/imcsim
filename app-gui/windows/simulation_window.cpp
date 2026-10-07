/**
 * @file    simulation_window.cpp
 * @brief   Window that runs the schematic through ngspice and shows the results and its output.
 */

#include "simulation_window.h"

#include "spice_value.h"
#include <algorithm>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr ImVec4 ErrorTextColor = {1.0f, 0.4f, 0.4f, 1.0f};
constexpr ImVec4 WarningTextColor = {1.0f, 0.8f, 0.4f, 1.0f};
// In font sizes, so the layout follows the DPI scale; matches the labels of ValueField
constexpr float LabelWidth = 6.0f;
constexpr float InputWidth = 8.0f;

bool IsPositive(const double value) {
    return value > 0.0;
}

// Shows why there is nothing to plot, or a warning when ngspice complained; returns whether to show the results
bool DrawRunStatus(const std::optional<std::string> &error, const std::vector<Core::SimulatorMessage> &messages,
                   const bool has_result) {
    if (error) {
        ImGui::TextColored(ErrorTextColor, "%s", error->c_str());
        return false;
    }
    if (!has_result) {
        ImGui::TextDisabled("No results for the current circuit; run the simulation");
        return false;
    }
    const bool has_warnings =
        std::ranges::any_of(messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
    if (has_warnings) {
        ImGui::TextColored(WarningTextColor, "ngspice reported warnings; check its output before trusting the "
                                             "results");
    }
    return true;
}

void DrawOutput(const std::vector<Core::SimulatorMessage> &messages) {
    if (messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(WarningTextColor, "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic to simulate; it must outlive the window.
 */
SimulationWindow::SimulationWindow(Schematic &schematic) : AppWindow("Simulation", true), m_Schematic(schematic) {
    m_StopTime.Load(m_TransientSettings.StopTime);
    m_TimeStep.Load(m_TransientSettings.TimeStep);
    m_StartFrequency.Load(m_ACSweepSettings.StartFrequency);
    m_StopFrequency.Load(m_ACSweepSettings.StopFrequency);
}

void SimulationWindow::Draw() {
    if (!ImGui::BeginTabBar("analyses")) {
        return;
    }
    if (ImGui::BeginTabItem("Operating point")) {
        DrawOperatingPointTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Transient")) {
        DrawTransientTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        DrawACSweepTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void SimulationWindow::DrawOperatingPointTab() {
    if (ImGui::Button("Run operating point (.op)")) {
        RunOperatingPoint();
    }
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (DrawRunStatus(m_OperatingPointStatus.Error, m_OperatingPointStatus.Messages, operating_point.has_value())) {
        DrawNodeVoltages(*operating_point);
    }
    DrawOutput(m_OperatingPointStatus.Messages);
}

// Settings change as soon as a field is valid; ngspice checks how they relate only when the analysis runs
void SimulationWindow::DrawTransientTab() {
    if (const std::optional<double> stop_time = m_StopTime.Draw("Stop time", "s", IsPositive)) {
        m_TransientSettings.StopTime = *stop_time;
    }
    if (const std::optional<double> time_step = m_TimeStep.Draw("Time step", "s", IsPositive)) {
        m_TransientSettings.TimeStep = *time_step;
    }
    if (ImGui::Button("Run transient (.tran)")) {
        RunTransient();
    }
    const auto &transient = m_Schematic.GetTransient();
    if (DrawRunStatus(m_TransientStatus.Error, m_TransientStatus.Messages, transient.has_value())) {
        ImGui::TextUnformatted("Done; the voltages are in the Output window");
    }
    DrawOutput(m_TransientStatus.Messages);
}

void SimulationWindow::DrawACSweepTab() {
    if (const std::optional<double> start = m_StartFrequency.Draw("Start", "Hz", IsPositive)) {
        m_ACSweepSettings.StartFrequency = *start;
    }
    if (const std::optional<double> stop = m_StopFrequency.Draw("Stop", "Hz", IsPositive)) {
        m_ACSweepSettings.StopFrequency = *stop;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Points");
    ImGui::SameLine(ImGui::GetFontSize() * LabelWidth);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * InputWidth);
    ImGui::InputInt("per decade", &m_ACSweepSettings.PointsPerDecade);
    if (ImGui::Button("Run AC sweep (.ac)")) {
        RunACSweep();
    }
    const auto &sweep = m_Schematic.GetACSweep();
    if (DrawRunStatus(m_ACSweepStatus.Error, m_ACSweepStatus.Messages, sweep.has_value())) {
        ImGui::TextUnformatted("Done; the Bode plot is in the Output window");
    }
    DrawOutput(m_ACSweepStatus.Messages);
}

// A rejected run also clears the previous result, so stale numbers never sit next to an error
void SimulationWindow::RunOperatingPoint() {
    Core::OperatingPointRun run = Core::RunOperatingPoint(m_Schematic.BuildCircuit());
    m_OperatingPointStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_OperatingPointStatus.Error.reset();
        m_Schematic.SetOperatingPoint(std::move(*run.Result));
    } else {
        m_OperatingPointStatus.Error = std::move(run.Result.error());
        m_Schematic.SetOperatingPoint(std::nullopt);
    }
}

void SimulationWindow::RunTransient() {
    Core::TransientRun run = Core::RunTransient(m_Schematic.BuildCircuit(), m_TransientSettings);
    m_TransientStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_TransientStatus.Error.reset();
        m_Schematic.SetTransient(std::move(*run.Result));
    } else {
        m_TransientStatus.Error = std::move(run.Result.error());
        m_Schematic.SetTransient(std::nullopt);
    }
}

void SimulationWindow::RunACSweep() {
    Core::ACSweepRun run = Core::RunACSweep(m_Schematic.BuildCircuit(), m_ACSweepSettings);
    m_ACSweepStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_ACSweepStatus.Error.reset();
        m_Schematic.SetACSweep(std::move(*run.Result));
    } else {
        m_ACSweepStatus.Error = std::move(run.Result.error());
        m_Schematic.SetACSweep(std::nullopt);
    }
}

void SimulationWindow::DrawNodeVoltages(const Core::OperatingPoint &operating_point) const {
    if (!ImGui::BeginTable("node_voltages", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Node");
    ImGui::TableSetupColumn("Voltage");
    ImGui::TableHeadersRow();
    for (std::size_t node = 0; node < operating_point.NodeVoltages.size(); ++node) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(node == 0 ? "0 (ground)" : std::format("{}", node).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(std::format("{}V", Core::FormatValue(operating_point.NodeVoltages[node])).c_str());
    }
    ImGui::EndTable();
}

} // namespace GUI
