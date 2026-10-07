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
    if (m_InitialSize) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(*m_InitialSize * ImGui::GetFontSize(), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
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

/**
 * @brief   Gives a floating window a size, centered on the main viewport, for the first time it appears.
 * @param[in] size  Width and height in font sizes, so they follow the DPI scale.
 * @note    Afterwards imgui.ini keeps where the user left it. Docked windows take the size of their dock node.
 */
void AppWindow::SetInitialSize(const ImVec2 size) {
    m_InitialSize = size;
}

} // namespace GUI
