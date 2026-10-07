/**
 * @file    app_window.cpp
 * @brief   Base class for the dockable ImGui windows of the application.
 */

#include "app_window.h"

#include <utility>

namespace GUI {

/**
 * @brief   Creates an open window.
 * @param[in] title     Window title, also used as its ImGui ID; keep it unique and stable.
 * @param[in] closable  Whether the title bar shows a close button.
 * @param[in] flags     ImGui window flags.
 */
AppWindow::AppWindow(std::string title, const bool closable, const ImGuiWindowFlags flags)
    : m_WindowTitle(std::move(title)), m_Closable(closable), m_WindowFlags(flags) {
}

/**
 * @brief   Draws the window and its content. Call once per frame.
 * @note    ImGui::Begin() returns false when the window is collapsed or hidden behind another dock tab; the
 *          content is skipped then, but ImGui::End() must always be called.
 */
void AppWindow::Render() {
    if (!m_IsOpen) {
        return;
    }
    if (ImGui::Begin(m_WindowTitle.c_str(), m_Closable ? &m_IsOpen : nullptr, m_WindowFlags)) {
        Draw();
    }
    ImGui::End();
}

/**
 * @brief   Tells whether the window is shown.
 * @return  False after the user closes it, until SetOpen(true).
 */
bool AppWindow::IsOpen() const {
    return m_IsOpen;
}

/**
 * @brief   Shows or hides the window.
 * @param[in] open  True to show it.
 */
void AppWindow::SetOpen(const bool open) {
    m_IsOpen = open;
}

/**
 * @brief   Returns the window title.
 * @return  The title, which is also the ImGui ID used by the docking layout.
 */
const std::string &AppWindow::GetWindowTitle() const {
    return m_WindowTitle;
}

} // namespace GUI
