/**
 * @file    app_window.h
 * @brief   Base class for the dockable ImGui windows of the application.
 */

#ifndef IMCSIM_APP_WINDOW_H
#define IMCSIM_APP_WINDOW_H

#include "imgui.h"
#include <optional>
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
    void OpenBehind();
    void Focus();
    const std::string &GetWindowTitle() const;

protected:
    void SetInitialSize(ImVec2 size);
    void SetNewContentMarker(bool shown);

private:
    virtual void Draw() = 0;

    std::string m_WindowTitle;
    bool m_Closable;
    ImGuiWindowFlags m_WindowFlags;
    bool m_IsOpen = true;
    // Brings the window to the front on its next Render(), as the selected tab when docked
    bool m_FocusRequested = false;
    // Opened by OpenBehind(): appears without taking the focus, and behind the selected tab when docked
    bool m_AppearBehind = false;
    // A dot beside the title or tab, for something new the user has not seen yet
    bool m_NewContentMarker = false;
    // In font sizes; only used the first time the window appears, before imgui.ini remembers it
    std::optional<ImVec2> m_InitialSize;
};

} // namespace GUI

#endif // IMCSIM_APP_WINDOW_H
