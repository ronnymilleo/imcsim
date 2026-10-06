/**
 * @file    editor.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_H
#define IMCSIM_EDITOR_H

#include "component.h"
#include "imgui.h"
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   Editor
 * @brief   Schematic editor window where circuit components are placed on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor.
 */
class Editor {
public:
    Editor() = default;
    ~Editor() = default;

    void EnableGrid() const;
    void Draw();

private:
    std::vector<Core::Component> m_Components;
    std::optional<Core::ComponentType> m_PlacingType;
    int m_PlacingRotation = 0;
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;

    ImVec2 ToScreen(ImVec2 origin, ImVec2 world_pos) const;
    ImVec2 ToWorld(ImVec2 origin, ImVec2 screen_pos) const;
    ImVec2 Snap(ImVec2 world_pos);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_H
