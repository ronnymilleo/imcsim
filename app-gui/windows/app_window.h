/**
 * @file    app_window.h
 * @brief   Base class for the dockable ImGui windows of the application.
 */

#ifndef IMCSIM_APP_WINDOW_H
#define IMCSIM_APP_WINDOW_H

#include "imgui.h"
#include <string>

namespace GUI {

/**
 * @class   AppWindow
 * @brief   An ImGui window that opens and closes its own Begin/End pair around the content of a derived class.
 * @details Derived classes implement Draw() with the window content only. The title doubles as the ImGui ID,
 *          so it also names the window in the docking layout and in imgui.ini.
 */
class AppWindow {
public:
    AppWindow(std::string title, bool closable, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
    virtual ~AppWindow() = default;

    void Render();
    bool IsOpen() const;
    void SetOpen(bool open);
    const std::string &GetWindowTitle() const;

private:
    virtual void Draw() = 0;

    std::string m_WindowTitle;
    bool m_Closable;
    ImGuiWindowFlags m_WindowFlags;
    bool m_IsOpen = true;
};

} // namespace GUI

#endif // IMCSIM_APP_WINDOW_H
