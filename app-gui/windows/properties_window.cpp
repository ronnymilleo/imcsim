/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "windows/properties_window.h"

#include "imgui.h"

namespace GUI {

/**
 * @brief   Creates the window, open beside Output in the default layout.
 * @param[in] editor  Part editor shared with the editor popover; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(PartEditor &editor) : AppWindow("Properties", true), m_Editor(editor) {
}

/**
 * @brief   Draws the part editor with its notes wrapped at the edge, since the column is narrow and a note cut off
 *          would lose its end.
 */
void PropertiesWindow::Draw() {
    ImGui::PushTextWrapPos(0.0f);
    m_Editor.Draw();
    ImGui::PopTextWrapPos();
}

} // namespace GUI
