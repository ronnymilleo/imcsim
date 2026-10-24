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
 * @param[in] controls   Runs of the analyses, shared with the editor toolbar; they must outlive the window.
 * @param[in] settings_panel  Analysis picker and settings, shared with the editor toolbar; it must outlive the window.
 */
SimulationWindow::SimulationWindow(Schematic &schematic, AnalysisControls &controls,
                                   AnalysisSettingsPanel &settings_panel)
    : AppWindow("Analysis Settings", true), m_Schematic(schematic), m_Controls(controls),
      m_SettingsPanel(settings_panel) {
    SetOpen(false);
}

void SimulationWindow::Draw() {
    m_SettingsPanel.DrawAnalysisPicker();
    ImGui::Separator();
    m_SettingsPanel.DrawSettings();
    const char *label = "Run (F5)";
    if (m_Controls.GetRunAction() == RunAction::STOP) {
        label = "Stop";
    } else if (m_Controls.GetRunAction() == RunAction::CONTINUE) {
        label = "Continue (F5)";
    }
    if (PrimaryButton(label)) {
        m_Controls.PressRun();
    }
    ImGui::Separator();
    m_Controls.DrawRunReport(m_Schematic.GetSimulationSettings().Selected);
}

} // namespace GUI
