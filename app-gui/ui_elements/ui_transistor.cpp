/**
 * @file    ui_transistor.cpp
 * @brief   Geometry and drawing shared by the schematic symbols of three-terminal transistors.
 */

#include "ui_elements/ui_transistor.h"

#include <utility>

namespace GUI {

namespace {

constexpr float LABEL_OFFSET_X = 1.3f;
constexpr float LABEL_OFFSET_Y = 0.5f;
/// Bounds reach a little past the right-hand leads, where the terminals sit.
constexpr float BOUNDS_RIGHT = 1.2f;

} // namespace

/**
 * @brief   Creates a transistor placed on the grid, together with its simulation component.
 * @param[in] component  Simulation component, a bipolar transistor or a MOSFET.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UITransistor::UITransistor(std::unique_ptr<Core::Component> component,
                           const GridPoint position,
                           const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

/**
 * @brief   Draws the three leads, from each terminal one unit toward the body.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UITransistor::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, -2), LocalToScreen(view, 1, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 1), LocalToScreen(view, 1, 2), color, LINE_THICKNESS);
}

/**
 * @brief   Draws the name and the model to the right of the body, in local orientation.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UITransistor::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(canvas, view, {LABEL_OFFSET_X, -LABEL_OFFSET_Y}, {1.0f, 0.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {LABEL_OFFSET_X, LABEL_OFFSET_Y}, {1.0f, 0.0f}, GetModelName(), color);
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
    return {{-2.0f, -2.0f}, {BOUNDS_RIGHT, 2.0f}};
}

} // namespace GUI
