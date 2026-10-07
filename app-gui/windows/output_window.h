/**
 * @file    output_window.h
 * @brief   Window that plots the transient and AC sweep results of the schematic.
 */

#ifndef IMCSIM_OUTPUT_WINDOW_H
#define IMCSIM_OUTPUT_WINDOW_H

#include "schematic.h"
#include "simulator.h"
#include "windows/app_window.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   OutputWindow
 * @brief   Plots the node voltages of the last transient over time, and the last AC sweep as a Bode plot.
 * @details The results come from the Schematic, which the Simulation window fills, so the plots disappear as
 *          soon as the circuit changes. The axes fit each new result, and keep zoom and pan until the next one.
 *          The window floats instead of being docked, so the plots can be as large as needed.
 */
class OutputWindow : public AppWindow {
public:
    explicit OutputWindow(Schematic &schematic);
    ~OutputWindow() override = default;

private:
    Schematic &m_Schematic;
    // Results version the axes were last fitted to
    std::optional<std::size_t> m_FittedResults;

    void Draw() override;
    void DrawTransient(const Core::Transient &transient, bool fit);
    void DrawACSweep(const Core::ACSweep &sweep, bool fit);
};

} // namespace GUI

#endif // IMCSIM_OUTPUT_WINDOW_H
