/**
 * @file    ui_transistor.cpp
 * @brief   Geometry and drawing shared by the schematic symbols of three-terminal transistors.
 */

#include "ui_transistor.h"

#include <cmath>
#include <utility>

namespace GUI {

namespace {

constexpr float LabelOffsetX = 1.3f;
constexpr float LabelOffsetY = 0.5f;
// Bounds reach a little past the right-hand leads, where the terminals sit
constexpr float BoundsRight = 1.2f;
constexpr float ArrowHeadLength = 0.3f;
constexpr float ArrowHeadHalfWidth = 0.15f;

} // namespace

/**
 * @brief   Creates a transistor placed on the grid, together with its simulation component.
 * @param[in] component  Simulation component, a bipolar transistor or a MOSFET.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UITransistor::UITransistor(std::unique_ptr<Core::Component> component, const GridPoint position,
                           const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

/**
 * @brief   Draws the three leads, from each terminal one unit toward the body.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UITransistor::DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, -2), LocalToScreen(view, 1, -1), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, 1, 1), LocalToScreen(view, 1, 2), color, LineThickness);
}

/**
 * @brief   Draws the name and the model to the right of the body, in local orientation.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UITransistor::DrawLabels(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(draw_list, view, {LabelOffsetX, -LabelOffsetY}, {1.0f, 0.0f}, GetComponent().GetName(), color);
    DrawLabel(draw_list, view, {LabelOffsetX, LabelOffsetY}, {1.0f, 0.0f}, GetModelName(), color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Collector or drain at (1, -2), base or gate at (-2, 0), emitter or source at (1, 2).
 */
std::vector<GridPoint> UITransistor::GetLocalTerminals() const {
    return {{1, -2}, {-2, 0}, {1, 2}};
}

/**
 * @brief   Returns the pick area of the transistor, in local grid units before rotation.
 * @return  A box around the body and the three terminals.
 */
LocalBounds UITransistor::GetLocalBounds() const {
    return {{-2.0f, -2.0f}, {BoundsRight, 2.0f}};
}

/**
 * @brief   Draws a filled arrow head, such as the one on an emitter.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] tip        Point of the arrow, in local grid units.
 * @param[in] direction  Direction the arrow points to, in local grid units; it does not need to be normalized.
 * @param[in] color      Fill color.
 */
void UITransistor::DrawArrowHead(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 tip,
                                 const ImVec2 direction, const ImU32 color) const {
    const float length = std::hypot(direction.x, direction.y);
    const ImVec2 along = {direction.x / length, direction.y / length};
    const ImVec2 across = {-along.y, along.x};
    const ImVec2 base = {tip.x - along.x * ArrowHeadLength, tip.y - along.y * ArrowHeadLength};
    draw_list->AddTriangleFilled(
        LocalToScreen(view, tip.x, tip.y),
        LocalToScreen(view, base.x + across.x * ArrowHeadHalfWidth, base.y + across.y * ArrowHeadHalfWidth),
        LocalToScreen(view, base.x - across.x * ArrowHeadHalfWidth, base.y - across.y * ArrowHeadHalfWidth), color);
}

} // namespace GUI
