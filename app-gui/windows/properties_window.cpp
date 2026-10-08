/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "properties_window.h"

namespace GUI {

/**
 * @brief   Creates the window, closed until the View menu opens it.
 * @param[in] editor  Part editor shared with the editor popover; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(PartEditor &editor) : AppWindow("Properties", true), m_Editor(editor) {
    SetOpen(false);
}

void PropertiesWindow::Draw() {
    m_Editor.Draw();
}

} // namespace GUI
