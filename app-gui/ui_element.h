/**
 * @file    ui_element.h
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#ifndef IMCSIM_UI_ELEMENT_H
#define IMCSIM_UI_ELEMENT_H

#include "component.h"
#include "imgui.h"

namespace GUI {

/**
 * @class   UIElement
 * @brief   Placement data (type, grid position and rotation) of a component drawn in the editor.
 */
class UIElement {
public:
    UIElement() = default;
    ~UIElement() = default;

private:
    Core::ComponentType m_ComponentType{};
    ImVec2 m_Position;
    int m_RotationDegrees{};
};

} // namespace GUI

#endif // IMCSIM_UI_ELEMENT_H
