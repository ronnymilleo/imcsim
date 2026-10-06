/**
 * @file    ui_element.cpp
 * @brief   Visual representation of a circuit component inside the schematic editor.
 */

#include "ui_element.h"

#include "spice_value.h"
#include <cfloat>
#include <cmath>
#include <utility>

namespace GUI {

namespace {

// Label font size relative to the zoom (pixels per grid unit)
constexpr float LabelScale = 0.7f;
// Below this font size labels are unreadable, so they are skipped
constexpr float MinLabelSize = 6.0f;
// Labels sit just outside the tallest two-terminal symbol, which spans y = -0.8 to 0.8
constexpr float LabelOffset = 1.0f;

} // namespace

/**
 * @brief   Creates an element for a component placed on the grid.
 * @param[in] component  Simulation component owned by this element.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UIElement::UIElement(std::unique_ptr<Core::Component> component, const GridPoint position, const Rotation rotation)
    : m_Component(std::move(component)), m_Position(position), m_Rotation(rotation) {
}

/**
 * @brief   Draws the terminals, the component symbol and its name and value labels.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color, so the same element can be drawn as a placement preview.
 * @param[in] style      Drawing standard for the symbol.
 */
void UIElement::Draw(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                     const SymbolStyle style) const {
    DrawTerminals(draw_list, view, color);
    DrawSymbol(draw_list, view, color, style);
    DrawLabels(draw_list, view, color);
}

/**
 * @brief   Returns the simulation component behind this element.
 * @return  The owned component.
 */
const Core::Component &UIElement::GetComponent() const {
    return *m_Component;
}

/**
 * @brief   Returns the simulation component behind this element, for editing its name or value.
 * @return  The owned component.
 */
Core::Component &UIElement::GetComponent() {
    return *m_Component;
}

/**
 * @brief   Returns the grid position of the element.
 * @return  Position in world units.
 */
GridPoint UIElement::GetPosition() const {
    return m_Position;
}

/**
 * @brief   Moves the element to another grid position.
 * @param[in] position  New grid position in world units.
 */
void UIElement::SetPosition(const GridPoint position) {
    m_Position = position;
}

/**
 * @brief   Returns the orientation of the element.
 * @return  The element rotation.
 */
Rotation UIElement::GetRotation() const {
    return m_Rotation;
}

/**
 * @brief   Changes the orientation of the element.
 * @param[in] rotation  New orientation on the grid.
 */
void UIElement::SetRotation(const Rotation rotation) {
    m_Rotation = rotation;
}

/**
 * @brief   Returns the terminals of the element on the grid, after rotation and translation.
 * @return  Connection points in world units, where wires can attach.
 */
std::vector<GridPoint> UIElement::GetTerminals() const {
    std::vector<GridPoint> terminals = GetLocalTerminals();
    for (GridPoint &terminal : terminals) {
        terminal = m_Position + Rotate(terminal, m_Rotation);
    }
    return terminals;
}

/**
 * @brief   Checks whether a world position falls on the element, for picking it with the mouse.
 * @param[in] world_pos  Position in world units, usually the cursor.
 * @return  True when the position is inside the symbol bounds, terminals included.
 */
bool UIElement::Contains(const ImVec2 world_pos) const {
    const ImVec2 local = Rotate(world_pos - ToVec2(m_Position), InverseRotation(m_Rotation));
    const LocalBounds bounds = GetLocalBounds();
    return local.x >= bounds.Min.x && local.x <= bounds.Max.x && local.y >= bounds.Min.y && local.y <= bounds.Max.y;
}

/**
 * @brief   Draws the leads of a two-terminal component, from x = -2 to -1 and from x = 1 to 2.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UIElement::DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LineThickness);
}

/**
 * @brief   Returns the terminals of a two-terminal component, in local grid units before rotation.
 * @return  The points at x = -2 and x = +2.
 */
std::vector<GridPoint> UIElement::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}};
}

/**
 * @brief   Returns the pick area of a two-terminal component, in local grid units before rotation.
 * @return  A box from terminal to terminal, as tall as the tallest symbol.
 */
LocalBounds UIElement::GetLocalBounds() const {
    return {{-2.0f, -0.8f}, {2.0f, 0.8f}};
}

/**
 * @brief   Draws the component name above the symbol and its value below, in local orientation.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIElement::DrawLabels(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(draw_list, view, {0.0f, -LabelOffset}, {0.0f, -1.0f}, m_Component->GetName(), color);
    if (m_Component->HasValue()) {
        DrawLabel(draw_list, view, {0.0f, LabelOffset}, {0.0f, 1.0f}, Core::FormatValue(m_Component->GetValue()),
                  color);
    }
}

// ImGui text cannot rotate, so the text stays upright and is pushed from the anchor along the rotated direction
// until it no longer overlaps the anchor, whatever the element rotation
void UIElement::DrawLabel(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 local_anchor,
                          const ImVec2 local_direction, const std::string &text, const ImU32 color) const {
    if (text.empty()) {
        return;
    }
    const ImVec2 anchor = LocalToScreen(view, local_anchor.x, local_anchor.y);
    const ImVec2 step = LocalToScreen(view, local_anchor.x + local_direction.x, local_anchor.y + local_direction.y);
    // The local direction is one grid unit long, so its length on screen is the current zoom
    const ImVec2 offset = step - anchor;
    const float zoom = std::hypot(offset.x, offset.y);
    const float font_size = zoom * LabelScale;
    if (font_size < MinLabelSize) {
        return;
    }

    const ImVec2 direction = offset / zoom;
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.c_str());
    const float half_extent = (std::abs(direction.x) * text_size.x + std::abs(direction.y) * text_size.y) / 2.0f;
    const ImVec2 center = anchor + direction * half_extent;
    draw_list->AddText(font, font_size, center - text_size / 2.0f, color, text.c_str());
}

/**
 * @brief   Converts a point of the symbol, in local grid units, to screen pixels.
 * @param[in] view  Transform of the current frame.
 * @param[in] x     Local X before rotation.
 * @param[in] y     Local Y before rotation.
 * @return  Position in screen pixels.
 */
ImVec2 UIElement::LocalToScreen(const ViewTransform &view, const float x, const float y) const {
    return view.ToScreen(ToVec2(m_Position) + Rotate(ImVec2{x, y}, m_Rotation));
}

} // namespace GUI
