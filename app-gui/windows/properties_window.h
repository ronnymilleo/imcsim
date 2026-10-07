/**
 * @file    properties_window.h
 * @brief   Window that shows and edits the component selected in the schematic editor.
 */

#ifndef IMCSIM_PROPERTIES_WINDOW_H
#define IMCSIM_PROPERTIES_WINDOW_H

#include "components/component.h"
#include "windows/app_window.h"
#include "windows/editor_window.h"
#include <array>
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   PropertiesWindow
 * @brief   Shows the type and name of the selected component and edits its value with SPICE suffixes.
 */
class PropertiesWindow : public AppWindow {
public:
    explicit PropertiesWindow(EditorWindow &editor);
    ~PropertiesWindow() override = default;

private:
    EditorWindow &m_Editor;
    // Text being edited, and the editor selection it was loaded from
    std::array<char, 32> m_ValueText{};
    std::optional<std::size_t> m_ValueTextSelection;
    bool m_ValueTextInvalid = false;

    void Draw() override;
    void LoadValueText(const Core::Component &component);
};

} // namespace GUI

#endif // IMCSIM_PROPERTIES_WINDOW_H
