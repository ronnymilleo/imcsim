/**
 * @file    simulation_window.cpp
 * @brief   Window with the settings of the analyses and the report of their last run.
 */

#include "simulation_window.h"

#include "theme.h"

namespace GUI {

/**
 * @brief   Creates the window, closed until the View menu opens it.
 * @param[in] schematic  Schematic whose analyses are set; it must outlive the window.
 * @param[in] controls   Analysis settings and runs shared with the editor toolbar; they must outlive the window.
 */
SimulationWindow::SimulationWindow(Schematic &schematic, AnalysisControls &controls)
    : AppWindow("Analysis Settings", true), m_Schematic(schematic), m_Controls(controls) {
    SetOpen(false);
}

void SimulationWindow::Draw() {
    m_Controls.DrawAnalysisPicker();
    ImGui::Separator();
    m_Controls.DrawSettings();
    if (PrimaryButton("Run (F5)")) {
        m_Controls.RunSelected();
    }
    ImGui::Separator();
    m_Controls.DrawRunReport(m_Schematic.GetSimulationSettings().Selected);
}

} // namespace GUI
