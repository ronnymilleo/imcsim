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

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic to simulate; it must outlive the window.
 */
SimulationWindow::SimulationWindow(Schematic &schematic) : AppWindow("Simulation", true), m_Schematic(schematic) {
}

void SimulationWindow::Draw() {
    if (ImGui::Button("Run operating point (.op)")) {
        RunOperatingPoint();
    }

    if (m_Error) {
        ImGui::TextColored(ErrorTextColor, "%s", m_Error->c_str());
    } else if (const auto &operating_point = m_Schematic.GetOperatingPoint()) {
        const bool has_warnings = std::ranges::any_of(
            m_Messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
        if (has_warnings) {
            ImGui::TextColored(WarningTextColor, "ngspice reported warnings; check its output before trusting "
                                                 "the voltages");
        }
        DrawNodeVoltages(*operating_point);
    } else {
        ImGui::TextDisabled("No results for the current circuit; run the simulation");
    }

    DrawOutput();
}

// A rejected run also clears the previous voltages, so stale numbers never sit next to an error
void SimulationWindow::RunOperatingPoint() {
    Core::OperatingPointRun run = Core::RunOperatingPoint(m_Schematic.BuildCircuit());
    m_Messages = std::move(run.Messages);
    if (run.Result) {
        m_Error.reset();
        m_Schematic.SetOperatingPoint(std::move(*run.Result));
    } else {
        m_Error = std::move(run.Result.error());
        m_Schematic.SetOperatingPoint(std::nullopt);
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

void SimulationWindow::DrawOutput() const {
    if (m_Messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : m_Messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(WarningTextColor, "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
}

} // namespace GUI
