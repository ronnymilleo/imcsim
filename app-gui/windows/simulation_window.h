/**
 * @file    simulation_window.h
 * @brief   Window with the settings of the analyses and the report of their last run.
 */

#ifndef IMCSIM_SIMULATION_WINDOW_H
#define IMCSIM_SIMULATION_WINDOW_H

#include "analysis_controls.h"
#include "schematic.h"
#include "windows/app_window.h"

namespace GUI {

/**
 * @class   SimulationWindow
 * @brief   The analysis picker and the settings of the selected analysis, its Run button and the report of its
 *          last run, with the ngspice output.
 * @details The editor toolbar runs the same analysis with the same settings; this window keeps them in view. It
 *          starts closed and opens from the View menu. Results are drawn by the editor and the Output window.
 */
class SimulationWindow : public AppWindow {
public:
    SimulationWindow(Schematic &schematic, AnalysisControls &controls);
    ~SimulationWindow() override = default;

private:
    Schematic &m_Schematic;
    AnalysisControls &m_Controls;

    void Draw() override;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_WINDOW_H
