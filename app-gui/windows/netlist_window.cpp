/**
 * @file    netlist_window.cpp
 * @brief   Window that shows the SPICE netlist of the schematic.
 */

#include "netlist_window.h"

#include <string>

namespace GUI {

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose netlist is shown; it must outlive the window.
 */
NetlistWindow::NetlistWindow(Schematic &schematic) : AppWindow("Netlist", true), m_Schematic(schematic) {
}

void NetlistWindow::Draw() {
    const std::string netlist = m_Schematic.BuildSpiceNetlist();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(netlist.c_str());
    }
    ImGui::Separator();
    ImGui::TextUnformatted(netlist.c_str());
}

} // namespace GUI
