/**
 * @file    ui_diode.cpp
 * @brief   Schematic symbol of a diode, a Zener diode or an LED.
 */

#include "ui_diode.h"

#include "components/diode.h"
#include <memory>

namespace GUI {

namespace {

// The triangle spans x = -0.5 to 0.5 and the bar sits at its tip
constexpr float BodyHalfLength = 0.5f;
constexpr float BodyHalfHeight = 0.6f;
constexpr float ZenerWingSize = 0.15f;
// LED arrows leave the upper side of the triangle at 45 degrees
constexpr float ArrowLength = 0.4f;
constexpr float ArrowHeadSize = 0.15f;
constexpr ImVec2 FirstArrowStart = {-0.2f, -0.7f};
constexpr ImVec2 SecondArrowStart = {0.15f, -0.7f};
// Labels clear the tallest part of the symbol, the arrows for an LED
constexpr float LabelOffset = 1.0f;
constexpr float LEDNameOffset = 1.3f;

} // namespace

/**
 * @brief   Creates a diode part placed on the grid, together with its simulation component.
 * @param[in] type      Core::ComponentType::Diode, ZenerDiode or LED.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIDiode::UIDiode(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Diode>(type), position, rotation) {
}

void UIDiode::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                         SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -BodyHalfLength, 0), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, BodyHalfLength, 0), LocalToScreen(view, 1, 0), color, LineThickness);
    canvas.AddTriangle(LocalToScreen(view, -BodyHalfLength, -BodyHalfHeight),
                       LocalToScreen(view, -BodyHalfLength, BodyHalfHeight), LocalToScreen(view, BodyHalfLength, 0),
                       color, LineThickness);
    canvas.AddLine(LocalToScreen(view, BodyHalfLength, -BodyHalfHeight),
                   LocalToScreen(view, BodyHalfLength, BodyHalfHeight), color, LineThickness);

    const Core::ComponentType type = GetComponent().GetType();
    if (type == Core::ComponentType::ZenerDiode) {
        // The ends of the bar bend in opposite directions, like a Z
        canvas.AddLine(LocalToScreen(view, BodyHalfLength, -BodyHalfHeight),
                       LocalToScreen(view, BodyHalfLength - ZenerWingSize, -BodyHalfHeight - ZenerWingSize), color,
                       LineThickness);
        canvas.AddLine(LocalToScreen(view, BodyHalfLength, BodyHalfHeight),
                       LocalToScreen(view, BodyHalfLength + ZenerWingSize, BodyHalfHeight + ZenerWingSize), color,
                       LineThickness);
    } else if (type == Core::ComponentType::LED) {
        DrawArrow(canvas, view, FirstArrowStart, color);
        DrawArrow(canvas, view, SecondArrowStart, color);
    }
}

/**
 * @brief   Draws the diode name above the symbol and its model below, in local orientation.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIDiode::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &diode = static_cast<const Core::Diode &>(GetComponent());
    const float name_offset = diode.GetType() == Core::ComponentType::LED ? LEDNameOffset : LabelOffset;
    DrawLabel(canvas, view, {0.0f, -name_offset}, {0.0f, -1.0f}, diode.GetName(), color);
    DrawLabel(canvas, view, {0.0f, LabelOffset}, {0.0f, 1.0f}, diode.GetModelName(), color);
}

/**
 * @brief   Returns the pick area of the diode, in local grid units before rotation.
 * @return  The two-terminal box, taller above for the arrows of an LED.
 */
LocalBounds UIDiode::GetLocalBounds() const {
    LocalBounds bounds = UIElement::GetLocalBounds();
    if (GetComponent().GetType() == Core::ComponentType::LED) {
        bounds.Min.y = FirstArrowStart.y - ArrowLength;
    }
    return bounds;
}

void UIDiode::DrawArrow(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 start,
                        const ImU32 color) const {
    const ImVec2 tip = {start.x + ArrowLength, start.y - ArrowLength};
    canvas.AddLine(LocalToScreen(view, start.x, start.y), LocalToScreen(view, tip.x, tip.y), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, tip.x, tip.y), LocalToScreen(view, tip.x - ArrowHeadSize, tip.y), color,
                   LineThickness);
    canvas.AddLine(LocalToScreen(view, tip.x, tip.y), LocalToScreen(view, tip.x, tip.y + ArrowHeadSize), color,
                   LineThickness);
}

} // namespace GUI
