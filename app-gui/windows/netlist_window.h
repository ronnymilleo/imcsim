/**
 * @file    netlist_window.h
 * @brief   Window that shows the SPICE netlist of the schematic.
 */

#ifndef IMCSIM_NETLIST_WINDOW_H
#define IMCSIM_NETLIST_WINDOW_H

#include "schematic.h"
#include "windows/app_window.h"

namespace GUI {

/**
 * @class   NetlistWindow
 * @brief   Shows the SPICE netlist that will be handed to ngspice, with a button to copy it.
 * @details It starts closed and opens from the View menu, docked beside the Properties window.
 */
class NetlistWindow : public AppWindow {
public:
    explicit NetlistWindow(Schematic &schematic);
    ~NetlistWindow() override = default;

private:
    Schematic &m_Schematic;

    void Draw() override;
};

} // namespace GUI

#endif // IMCSIM_NETLIST_WINDOW_H
