/***********************************************************************************************************************
 * @file    editor.cpp
 * @brief
 * @details
 *
 * @project imcsim
 * @author  ronnymilleo
 * @date    10/6/26
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes
***********************************************************************************************************************/

#include "editor.h"
#include "imgui.h"

#include <algorithm>

using namespace GUI;

/***********************************************************************************************************************
 Method Definitions
***********************************************************************************************************************/

void Editor::Draw() {
    ImGui::Begin("Schematic");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 50)
        size.x = 50;
    if (size.y < 50)
        size.y = 50;

    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO &io = ImGui::GetIO();

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        m_Pan += io.MouseDelta;
    if (hovered && io.MouseWheel != 0.0f) {
        const ImVec2 before = ToWorld(origin, io.MousePos);
        m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? 1.1f : 1 / 1.1f), 4.0f, 200.0f);
        const ImVec2 after = ToWorld(origin, io.MousePos);
        m_Pan += (after - before) * m_Zoom;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, origin + size, true);
    dl->AddRectFilled(origin, origin + size, IM_COL32(30, 30, 35, 255));

    ImVec2 w0 = ToWorld(origin, origin);
    ImVec2 w1 = ToWorld(origin, origin + size);
    for (float x = std::floor(w0.x); x <= w1.x; ++x)
        for (float y = std::floor(w0.y); y <= w1.y; ++y)
            dl->AddCircleFilled(ToScreen(origin, {x, y}), 1.2f, IM_COL32(90, 90, 100, 255));

    dl->PopClipRect();
    ImGui::End();
}

ImVec2 Editor::ToScreen(const ImVec2 origin, const ImVec2 w) const {
    return origin + m_Pan + w * m_Zoom;
}
ImVec2 Editor::ToWorld(const ImVec2 origin, const ImVec2 s) const {
    return (s - origin - m_Pan) / m_Zoom;
}
ImVec2 Editor::Snap(const ImVec2 w) {
    return {std::round(w.x), std::round(w.y)};
}

/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/
