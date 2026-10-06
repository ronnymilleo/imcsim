/**
 * @file    editor.cpp
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#include "editor.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace GUI {

/**
 * @brief   Draws the editor window and handles pan and zoom input. Call once per frame.
 */
void Editor::Draw() {
    ImGui::Begin("Schematic");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 50) {
        size.x = 50;
    }
    if (size.y < 50) {
        size.y = 50;
    }

    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO &io = ImGui::GetIO();

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_Pan += io.MouseDelta;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        // Keep the world point under the cursor fixed while zooming
        const ImVec2 before = ToWorld(origin, io.MousePos);
        m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? 1.1f : 1 / 1.1f), 4.0f, 200.0f);
        const ImVec2 after = ToWorld(origin, io.MousePos);
        m_Pan += (after - before) * m_Zoom;
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, origin + size, true);
    draw_list->AddRectFilled(origin, origin + size, IM_COL32(30, 30, 35, 255));

    const ImVec2 world_min = ToWorld(origin, origin);
    const ImVec2 world_max = ToWorld(origin, origin + size);
    for (float x = std::floor(world_min.x); x <= world_max.x; ++x) {
        for (float y = std::floor(world_min.y); y <= world_max.y; ++y) {
            draw_list->AddCircleFilled(ToScreen(origin, {x, y}), 1.2f, IM_COL32(90, 90, 100, 255));
        }
    }

    draw_list->PopClipRect();
    ImGui::End();
}

ImVec2 Editor::ToScreen(const ImVec2 origin, const ImVec2 world_pos) const {
    return origin + m_Pan + world_pos * m_Zoom;
}

ImVec2 Editor::ToWorld(const ImVec2 origin, const ImVec2 screen_pos) const {
    return (screen_pos - origin - m_Pan) / m_Zoom;
}

ImVec2 Editor::Snap(const ImVec2 world_pos) {
    return {std::round(world_pos.x), std::round(world_pos.y)};
}

} // namespace GUI
