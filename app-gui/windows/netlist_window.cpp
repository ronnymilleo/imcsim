/**
 * @file    netlist_window.cpp
 * @brief   Window that shows the SPICE netlist of the schematic being edited.
 */

#include "netlist_window.h"

#include <string>

namespace GUI {

/**
 * @brief   Creates the window for an editor.
 * @param[in] editor  Editor whose schematic is shown; it must outlive the window.
 */
NetlistWindow::NetlistWindow(EditorWindow &editor) : AppWindow("Netlist", true), m_Editor(editor) {
}

void NetlistWindow::Draw() {
    const std::string netlist = m_Editor.BuildSpiceNetlist();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(netlist.c_str());
    }
    ImGui::Separator();
    ImGui::TextUnformatted(netlist.c_str());
}

} // namespace GUI
