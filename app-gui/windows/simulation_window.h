/**
 * @file    simulation_window.h
 * @brief   Window that runs the schematic through ngspice and shows the results and its output.
 */

#ifndef IMCSIM_SIMULATION_WINDOW_H
#define IMCSIM_SIMULATION_WINDOW_H

#include "schematic.h"
#include "simulator.h"
#include "windows/app_window.h"
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   SimulationWindow
 * @brief   Runs the DC operating point, lists the node voltages and keeps what ngspice printed.
 * @details The voltages live in the Schematic, so the editor can draw them too and they disappear as soon as
 *          the circuit changes. The error and the output belong to the last run and stay until the next one.
 */
class SimulationWindow : public AppWindow {
public:
    explicit SimulationWindow(Schematic &schematic);
    ~SimulationWindow() override = default;

private:
    Schematic &m_Schematic;
    std::optional<std::string> m_Error;
    std::vector<Core::SimulatorMessage> m_Messages;

    void Draw() override;
    void RunOperatingPoint();
    void DrawNodeVoltages(const Core::OperatingPoint &operating_point) const;
    void DrawOutput() const;
};

} // namespace GUI

#endif // IMCSIM_SIMULATION_WINDOW_H
