/**
 * @file    ui_single_terminal_element.cpp
 * @brief   Geometry shared by the schematic symbols with one terminal, such as ground, supplies and labels.
 */

#include "ui_elements/ui_single_terminal_element.h"

#include <utility>

namespace GUI {

namespace {

/**
 * @brief   The sign of a coordinate, as a grid step.
 */
int GetStep(const float value) {
    return value > 0.0f ? 1 : value < 0.0f ? -1 : 0;
}

} // namespace

/**
 * @brief   Creates a single-terminal symbol placed on the grid.
 * @param[in] component  Simulation component of the part.
 * @param[in] position   Grid position of the terminal, in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UISingleTerminalElement::UISingleTerminalElement(std::unique_ptr<Core::Component> component,
                                                 const GridPoint position,
                                                 const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

/**
 * @brief   Draws the lead from the terminal to the body.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UISingleTerminalElement::DrawTerminals(SchematicCanvas &canvas,
                                            const ViewTransform &view,
                                            const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, GetLeadEnd()), color, LINE_THICKNESS);
}

/**
 * @brief   Returns the terminal, in local grid units before rotation.
 * @return  The origin.
 */
std::vector<GridPoint> UISingleTerminalElement::GetLocalTerminals() const {
    return {{0, 0}};
}

/**
 * @brief   The way into the part runs along the lead.
 * @param[in] terminal  Index in `GetLocalTerminals()` (always 0).
 * @return  A unit step from the terminal toward the end of the lead.
 */
GridPoint UISingleTerminalElement::GetLocalTerminalInward([[maybe_unused]] const std::size_t terminal) const {
    const ImVec2 lead_end = GetLeadEnd();
    return {GetStep(lead_end.x), GetStep(lead_end.y)};
}

} // namespace GUI
