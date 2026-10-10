/**
 * @file    app_window.cpp
 * @brief   Base class for the dockable ImGui windows of the application.
 */

#include "app_window.h"

#include "imgui_internal.h"
#include <utility>

namespace GUI {

namespace {

// A dock node selects every tab added to it on its next update. Adding the tab of the current window now, in the
// frame it docks, leaves nothing new for that update, so the selected tab stays in front
void KeepTabBehind() {
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    ImGuiDockNode *node = window->DockNode;
    if (node == nullptr || node->TabBar == nullptr ||
        ImGui::TabBarFindTabByID(node->TabBar, window->TabId) != nullptr) {
        return;
    }
    ImGui::TabBarAddTab(node->TabBar, ImGuiTabItemFlags_Unsorted, window);
}

} // namespace

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
    if (std::exchange(m_FocusRequested, false)) {
        ImGui::SetNextWindowFocus();
    }
    const bool appear_behind = std::exchange(m_AppearBehind, false);
    ImGuiWindowFlags flags = m_WindowFlags | (m_NewContentMarker ? ImGuiWindowFlags_UnsavedDocument : 0);
    if (appear_behind) {
        flags |= ImGuiWindowFlags_NoFocusOnAppearing;
    }
    if (ImGui::Begin(m_WindowTitle.c_str(), m_Closable ? &m_IsOpen : nullptr, flags)) {
        Draw();
    }
    if (appear_behind) {
        KeepTabBehind();
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
 * @brief   Opens the window, if closed, without bringing it to the front.
 * @note    A docked window joins its dock node as a tab behind the selected one; a floating window appears
 *          without taking the keyboard focus.
 */
void AppWindow::OpenBehind() {
    if (!m_IsOpen) {
        m_IsOpen = true;
        m_AppearBehind = true;
    }
}

/**
 * @brief   Brings the window to the front the next time it is drawn.
 * @note    A docked window becomes the selected tab of its dock node.
 */
void AppWindow::Focus() {
    m_FocusRequested = true;
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

/**
 * @brief   Shows or hides a dot beside the window title, or beside its tab when docked.
 * @param[in] shown  True while the window holds something new the user has not seen.
 */
void AppWindow::SetNewContentMarker(const bool shown) {
    m_NewContentMarker = shown;
}

} // namespace GUI
