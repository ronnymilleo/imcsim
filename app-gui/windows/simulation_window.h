/**
 * @file    simulation_window.h
 * @brief   Window that runs the schematic through ngspice and shows the results and its output.
 */

#ifndef IMCSIM_SIMULATION_WINDOW_H
#define IMCSIM_SIMULATION_WINDOW_H

#include "schematic.h"
#include "simulator.h"
#include "value_field.h"
#include "windows/app_window.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   SimulationWindow
 * @brief   Runs the operating point, transient and AC sweep analyses, one tab each, with their settings.
 * @details The results live in the Schematic, so they disappear as soon as the circuit changes. The node
 *          voltages of the operating point are listed here and drawn by the editor; transients and AC sweeps
 *          are drawn by the Output window. The error and the output belong to the last run of each analysis
 *          and stay until its next run.
 */
class SimulationWindow : public AppWindow {
public:
    explicit SimulationWindow(Schematic &schematic);
    ~SimulationWindow() override = default;

private:
    /**
     * @struct  RunStatus
     * @brief   What the last run of one analysis left besides its result.
     */
    struct RunStatus {
        std::optional<std::string> Error;
        std::vector<Core::SimulatorMessage> Messages;
    };

    Schematic &m_Schematic;

    // Operating point
    RunStatus m_OperatingPointStatus;

    // Transient
    Core::TransientSettings m_TransientSettings;
    ValueField m_StopTime;
    ValueField m_TimeStep;
    RunStatus m_TransientStatus;

    // AC sweep
    Core::ACSweepSettings m_ACSweepSettings;
    ValueField m_StartFrequency;
    ValueField m_StopFrequency;
    RunStatus m_ACSweepStatus;

    void Draw() override;

    // Tabs
    void DrawOperatingPointTab();
    void DrawTransientTab();
    void DrawACSweepTab();

    // Runs
    void RunOperatingPoint();
    void RunTransient();
    void RunACSweep();

    // Results
    void DrawNodeVoltages(const Core::OperatingPoint &operating_point) const;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_WINDOW_H
