/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "windows/properties_window.h"

namespace GUI {

/**
 * @brief   Creates the window, open beside Output in the default layout.
 * @param[in] editor  Part editor shared with the editor popover; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(PartEditor &editor) : AppWindow("Properties", true), m_Editor(editor) {
}

void PropertiesWindow::Draw() {
    m_Editor.Draw();
}

} // namespace GUI
