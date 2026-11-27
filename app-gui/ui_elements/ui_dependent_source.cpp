/**
 * @file    ui_dependent_source.cpp
 * @brief   Drawing shared by the schematic symbols of dependent sources: the behavioral and the controlled ones.
 */

#include "ui_elements/ui_dependent_source.h"

#include <utility>

namespace GUI {

namespace {

/// Distance of the signs inside the diamond above and below its middle.
constexpr float INNER_SIGN_OFFSET = 0.4f;
constexpr float ARROW_HALF_LENGTH = 0.45f;
constexpr ArrowHead ARROW_HEAD = {.Length = 0.2f, .HalfWidth = 0.15f};
constexpr float LABEL_OFFSET_X = 1.1f;
constexpr float LABEL_OFFSET_Y = 0.5f;

} // namespace

/**
 * @brief   Creates a dependent source placed on the grid.
 * @param[in] component  Simulation component of the source.
 * @param[in] position   Grid position in world units.
 * @param[in] rotation   Orientation on the grid.
 */
UIDependentSource::UIDependentSource(std::unique_ptr<Core::Component> component,
                                     const GridPoint position,
                                     const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

/**
 * @brief   The output leads reach the diamond from both terminals.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 */
void UIDependentSource::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -DIAMOND_RADIUS), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, DIAMOND_RADIUS), LocalToScreen(view, 0, 2), color, LINE_THICKNESS);
}

/**
 * @brief   A source that sets a voltage shows its polarity, + toward terminal 1; one that sets a current shows the way
 *          it pushes it, from terminal 1 to terminal 2 through itself, as SPICE does.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 * @param[in]     style   Drawing standard (unused; diamond symbol is identical in IEC and ANSI).
 */
void UIDependentSource::DrawSymbol(SchematicCanvas &canvas,
                                   const ViewTransform &view,
                                   const ImU32 color,
                                   [[maybe_unused]] const SymbolStyle style) const {
    const ImVec2 diamond[4] = {LocalToScreen(view, 0, -DIAMOND_RADIUS),
                               LocalToScreen(view, DIAMOND_RADIUS, 0),
                               LocalToScreen(view, 0, DIAMOND_RADIUS),
                               LocalToScreen(view, -DIAMOND_RADIUS, 0)};
    canvas.AddPolyline(diamond, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    if (SetsVoltage()) {
        DrawSign(canvas, view, {0.0f, -INNER_SIGN_OFFSET}, true, color);
        DrawSign(canvas, view, {0.0f, INNER_SIGN_OFFSET}, false, color);
        return;
    }
    DrawArrow(canvas, view, {0.0f, -ARROW_HALF_LENGTH}, {0.0f, ARROW_HALF_LENGTH}, ARROW_HEAD, color);
}

/**
 * @brief   Returns the output terminals, in local grid units before rotation.
 * @return  Terminal 1 at (0, -2) and terminal 2 at (0, 2).
 */
std::vector<GridPoint> UIDependentSource::GetLocalTerminals() const {
    return {{0, -2}, {0, 2}};
}

/**
 * @brief   Draws the name above and the value below, both to the right of the diamond.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     value   What the source drives, such as its gain or its expression.
 * @param[in]     color   Text color.
 */
void UIDependentSource::DrawNameAndValue(SchematicCanvas &canvas,
                                         const ViewTransform &view,
                                         const std::string &value,
                                         const ImU32 color) const {
    DrawLabel(canvas, view, {LABEL_OFFSET_X, -LABEL_OFFSET_Y}, {1.0f, 0.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {LABEL_OFFSET_X, LABEL_OFFSET_Y}, {1.0f, 0.0f}, value, color);
}

} // namespace GUI
