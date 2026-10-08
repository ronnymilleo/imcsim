/**
 * @file    simulation_window.h
 * @brief   Window that runs the schematic through ngspice and shows the results and its output.
 */

#ifndef IMCSIM_SIMULATION_WINDOW_H
#define IMCSIM_SIMULATION_WINDOW_H

#include "schematic.h"
#include "simulation_settings.h"
#include "simulator.h"
#include "value_field.h"
#include "windows/app_window.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   SimulationWindow
 * @brief   Runs the operating point, transient and AC sweep analyses, one tab each, with their settings.
 * @details The settings and the results live in the Schematic: the settings are saved with it, and the results
 *          disappear as soon as the circuit changes. The node voltages and component currents of the operating
 *          point are listed here, and the voltages are also drawn by the editor; transients and AC sweeps are
 *          drawn by the Output window. The error and the output belong to the last run of each analysis and stay
 *          until its next run.
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

    // Settings fields, reloaded whenever the schematic replaces its settings
    std::size_t m_LoadedSettingsVersion = 0;
    ValueField m_StopTime;
    ValueField m_TimeStep;
    ValueField m_StartFrequency;
    ValueField m_StopFrequency;
    // Start, stop and step of each DC sweep range
    std::array<ValueField, 3> m_SweptFields;
    std::array<ValueField, 3> m_SteppedFields;

    // Last run of each analysis
    RunStatus m_OperatingPointStatus;
    RunStatus m_TransientStatus;
    RunStatus m_ACSweepStatus;
    RunStatus m_DCSweepStatus;

    void Draw() override;
    void LoadSettingsFields();

    // Tabs
    void DrawOperatingPointTab();
    void DrawTransientTab(Core::TransientSettings &settings);
    void DrawACSweepTab(Core::ACSweepSettings &settings);
    void DrawDCSweepTab(SimulationSettings &settings);

    // Runs
    void RunOperatingPoint();
    void RunTransient(const Core::TransientSettings &settings);
    void RunACSweep(const Core::ACSweepSettings &settings);
    void RunDCSweep(const Core::DCSweepSettings &settings);

    // Results
    void DrawNodeVoltages(const Core::OperatingPoint &operating_point) const;
    void DrawCurrents(const Core::OperatingPoint &operating_point) const;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_WINDOW_H
