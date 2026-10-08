/**
 * @file    netlist_window.cpp
 * @brief   Window that shows the SPICE netlist of the schematic.
 */

#include "netlist_window.h"

#include "theme.h"
#include <string>

namespace GUI {

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose netlist is shown; it must outlive the window.
 * @note    It starts closed, for those who want to read SPICE; View opens it.
 */
NetlistWindow::NetlistWindow(Schematic &schematic) : AppWindow("Netlist", true), m_Schematic(schematic) {
    SetOpen(false);
}

void NetlistWindow::Draw() {
    const std::string netlist = m_Schematic.BuildSpiceNetlist();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(netlist.c_str());
    }
    ImGui::Separator();
    ImGui::PushFont(GetMonospaceFont(), 0.0f);
    ImGui::TextUnformatted(netlist.c_str());
    ImGui::PopFont();
}

} // namespace GUI
