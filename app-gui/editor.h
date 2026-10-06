/***********************************************************************************************************************
 * @file    editor.h
 * @brief
 * @details
 *
 * @project imcsim
 * @author  ronnymilleo
 * @date    10/6/26
 ***********************************************************************************************************************/

#ifndef IMCSIM_EDITOR_H
#define IMCSIM_EDITOR_H

/***********************************************************************************************************************
 Includes
***********************************************************************************************************************/

#include "component.h"
#include "imgui.h"
#include <cmath>
#include <optional>
#include <vector>

/***********************************************************************************************************************
 Class
***********************************************************************************************************************/

namespace GUI {
/**
 * @class   Editor
 * @brief
 * @details
 *
 * @note
 */
class Editor {
public:
    Editor() = default;
    ~Editor() = default;
    void EnableGrid() const;
    void Draw();

private:
    std::vector<Core::Component> m_ComponentsVector;
    std::optional<Core::ComponentType> m_IsPlacing;
    int m_PlacingRotation = 0;
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = 20.0f;

    ImVec2 ToScreen(ImVec2 origin, ImVec2 w) const;
    ImVec2 ToWorld(ImVec2 origin, ImVec2 s) const;
    ImVec2 Snap(ImVec2 w);
};
} // namespace GUI

#endif // IMCSIM_EDITOR_H
/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/
