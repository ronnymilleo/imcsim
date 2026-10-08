/**
 * @file    properties_window.h
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#ifndef IMCSIM_PROPERTIES_WINDOW_H
#define IMCSIM_PROPERTIES_WINDOW_H

#include "part_editor.h"
#include "windows/app_window.h"

namespace GUI {

/**
 * @class   PropertiesWindow
 * @brief   Keeps the part editor of the selected component docked in view.
 * @details The editor popover edits the same component with the same fields; this window starts closed and opens
 *          from the View menu, for those who prefer a fixed panel.
 */
class PropertiesWindow : public AppWindow {
public:
    explicit PropertiesWindow(PartEditor &editor);
    ~PropertiesWindow() override = default;

private:
    PartEditor &m_Editor;

    void Draw() override;
};

} // namespace GUI

#endif // IMCSIM_PROPERTIES_WINDOW_H
