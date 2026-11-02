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
constexpr float BODY_HALF_LENGTH = 0.5f;
constexpr float BODY_HALF_HEIGHT = 0.6f;
constexpr float ZENER_WING_SIZE = 0.15f;
// LED arrows leave the upper side of the triangle at 45 degrees
constexpr float ARROW_LENGTH = 0.4f;
constexpr float ARROW_HEAD_SIZE = 0.15f;
constexpr ImVec2 FIRST_ARROW_START = {-0.2f, -0.7f};
constexpr ImVec2 SECOND_ARROW_START = {0.15f, -0.7f};
// Labels clear the tallest part of the symbol, the arrows for an LED
constexpr float LABEL_OFFSET = 1.0f;
constexpr float LED_NAME_OFFSET = 1.3f;

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
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -BODY_HALF_LENGTH, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, 0), LocalToScreen(view, 1, 0), color, LINE_THICKNESS);
    canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                       LocalToScreen(view, -BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                       LocalToScreen(view, BODY_HALF_LENGTH, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                   LocalToScreen(view, BODY_HALF_LENGTH, BODY_HALF_HEIGHT), color, LINE_THICKNESS);

    const Core::ComponentType type = GetComponent().GetType();
    if (type == Core::ComponentType::ZENER_DIODE) {
        // The ends of the bar bend in opposite directions, like a Z
        canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                       LocalToScreen(view, BODY_HALF_LENGTH - ZENER_WING_SIZE, -BODY_HALF_HEIGHT - ZENER_WING_SIZE),
                       color, LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                       LocalToScreen(view, BODY_HALF_LENGTH + ZENER_WING_SIZE, BODY_HALF_HEIGHT + ZENER_WING_SIZE),
                       color, LINE_THICKNESS);
    } else if (type == Core::ComponentType::LED) {
        DrawArrow(canvas, view, FIRST_ARROW_START, color);
        DrawArrow(canvas, view, SECOND_ARROW_START, color);
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
    const float name_offset = diode.GetType() == Core::ComponentType::LED ? LED_NAME_OFFSET : LABEL_OFFSET;
    DrawLabel(canvas, view, {0.0f, -name_offset}, {0.0f, -1.0f}, diode.GetName(), color);
    DrawLabel(canvas, view, {0.0f, LABEL_OFFSET}, {0.0f, 1.0f}, diode.GetModelName(), color);
}

/**
 * @brief   Returns the pick area of the diode, in local grid units before rotation.
 * @return  The two-terminal box, taller above for the arrows of an LED.
 */
LocalBounds UIDiode::GetLocalBounds() const {
    LocalBounds bounds = UIElement::GetLocalBounds();
    if (GetComponent().GetType() == Core::ComponentType::LED) {
        bounds.Min.y = FIRST_ARROW_START.y - ARROW_LENGTH;
    }
    return bounds;
}

void UIDiode::DrawArrow(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 start,
                        const ImU32 color) const {
    const ImVec2 tip = {start.x + ARROW_LENGTH, start.y - ARROW_LENGTH};
    canvas.AddLine(LocalToScreen(view, start.x, start.y), LocalToScreen(view, tip.x, tip.y), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, tip.x, tip.y), LocalToScreen(view, tip.x - ARROW_HEAD_SIZE, tip.y), color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, tip.x, tip.y), LocalToScreen(view, tip.x, tip.y + ARROW_HEAD_SIZE), color,
                   LINE_THICKNESS);
}

} // namespace GUI
