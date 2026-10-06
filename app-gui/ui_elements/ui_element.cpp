/**
 * @file    ui_element.cpp
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#include "ui_element.h"

#include <utility>

namespace GUI {

/**
 * @brief   Creates an element for a component placed on the grid.
 * @param[in] component  Simulation component owned by this element.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UIElement::UIElement(std::unique_ptr<Core::Component> component, const ImVec2 position, const Rotation rotation)
    : m_Component(std::move(component)), m_Position(position), m_Rotation(rotation) {
}

/**
 * @brief   Draws the terminals and the component symbol.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color, so the same element can be drawn as a placement preview.
 * @param[in] style      Drawing standard for the symbol.
 */
void UIElement::Draw(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                     const SymbolStyle style) const {
    draw_list->AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LineThickness);
    DrawSymbol(draw_list, view, color, style);
}

/**
 * @brief   Returns the simulation component behind this element.
 * @return  The owned component.
 */
const Core::Component &UIElement::GetComponent() const {
    return *m_Component;
}

/**
 * @brief   Returns the grid position of the element.
 * @return  Position in world units.
 */
ImVec2 UIElement::GetPosition() const {
    return m_Position;
}

/**
 * @brief   Returns the orientation of the element.
 * @return  The element rotation.
 */
Rotation UIElement::GetRotation() const {
    return m_Rotation;
}

/**
 * @brief   Converts a point of the symbol, in local grid units, to screen pixels.
 * @param[in] view  Transform of the current frame.
 * @param[in] x     Local X before rotation.
 * @param[in] y     Local Y before rotation.
 * @return  Position in screen pixels.
 */
ImVec2 UIElement::LocalToScreen(const ViewTransform &view, const float x, const float y) const {
    return view.ToScreen(m_Position + Rotate({x, y}, m_Rotation));
}

} // namespace GUI
