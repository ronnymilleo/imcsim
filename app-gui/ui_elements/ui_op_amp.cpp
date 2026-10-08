/**
 * @file    ui_op_amp.cpp
 * @brief   Schematic symbol of an op-amp with its supply pins.
 */

#include "ui_op_amp.h"

#include "components/op_amp.h"
#include "spice_value.h"
#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

// The triangle spans x = -2 to 2, so its slanted sides cross x = 0 at y = -1 and 1, where the supply leads end
constexpr float BodyHalfLength = 2.0f;
constexpr float BodyHalfHeight = 2.0f;
constexpr float SupplyLeadEnd = 1.0f;
constexpr float SignX = -1.4f;
constexpr float SignHalfSize = 0.2f;
constexpr float LabelOffsetX = 0.6f;
constexpr float LabelOffsetY = 1.5f;

} // namespace

/**
 * @brief   Creates an op-amp placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIOpAmp::UIOpAmp(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::OpAmp>(), position, rotation) {
}

void UIOpAmp::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, BodyHalfLength, 0), LocalToScreen(view, 3, 0), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -3, -1), LocalToScreen(view, -BodyHalfLength, -1), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -3, 1), LocalToScreen(view, -BodyHalfLength, 1), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -SupplyLeadEnd), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, 0, SupplyLeadEnd), LocalToScreen(view, 0, 2), color, LineThickness);
}

// The input signs sit inside the triangle, next to their inputs
void UIOpAmp::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                         SymbolStyle /*style*/) const {
    const ImVec2 triangle[3] = {LocalToScreen(view, -BodyHalfLength, -BodyHalfHeight),
                                LocalToScreen(view, BodyHalfLength, 0),
                                LocalToScreen(view, -BodyHalfLength, BodyHalfHeight)};
    canvas.AddPolyline(triangle, 3, color, ImDrawFlags_Closed, LineThickness);
    for (const float y : {-1.0f, 1.0f}) {
        canvas.AddLine(LocalToScreen(view, SignX - SignHalfSize, y), LocalToScreen(view, SignX + SignHalfSize, y),
                       color, LineThickness);
    }
    canvas.AddLine(LocalToScreen(view, SignX, 1.0f - SignHalfSize), LocalToScreen(view, SignX, 1.0f + SignHalfSize),
                   color, LineThickness);
}

// The name above the triangle and the model below it, with its gain-bandwidth product while ideal
void UIOpAmp::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &op_amp = static_cast<const Core::OpAmp &>(GetComponent());
    std::string model = op_amp.GetModelName();
    if (op_amp.IsIdeal()) {
        model += std::format(" {}Hz", Core::FormatValue(op_amp.GetIdealBandwidth()));
    }
    DrawLabel(canvas, view, {LabelOffsetX, -LabelOffsetY}, {1.0f, 0.0f}, op_amp.GetName(), color);
    DrawLabel(canvas, view, {LabelOffsetX, LabelOffsetY}, {1.0f, 0.0f}, model, color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Output (3, 0), + input (-3, 1), - input (-3, -1), V+ (0, -2) and V- (0, 2).
 */
std::vector<GridPoint> UIOpAmp::GetLocalTerminals() const {
    return {{3, 0}, {-3, 1}, {-3, -1}, {0, -2}, {0, 2}};
}

/**
 * @brief   Returns the pick area of the op-amp, in local grid units before rotation.
 * @return  The box from the inputs to the output and between the supply pins.
 */
LocalBounds UIOpAmp::GetLocalBounds() const {
    return {{-3.0f, -2.0f}, {3.0f, 2.0f}};
}

} // namespace GUI
