/**
 * @file    ui_potentiometer.cpp
 * @brief   Schematic symbol of a potentiometer.
 */

#include "ui_potentiometer.h"

#include "components/potentiometer.h"
#include "spice_value.h"
#include <format>
#include <memory>

namespace GUI {

namespace {

// The arrow points down at the body, whose top edge is at y = -0.4
constexpr float ARROW_TIP_Y = -0.45f;
constexpr float ARROW_HEAD_LENGTH = 0.3f;
constexpr float ARROW_HEAD_HALF_WIDTH = 0.15f;
// The label hangs below the body
constexpr float LABEL_OFFSET_Y = 0.7f;

} // namespace

/**
 * @brief   Creates a potentiometer placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIPotentiometer::UIPotentiometer(const GridPoint position, const Rotation rotation)
    : UIResistor(std::make_unique<Core::Potentiometer>(), position, rotation) {
}

// The body of a resistor, and the wiper lead from the third terminal down to an arrow head on it
void UIPotentiometer::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 const SymbolStyle style) const {
    UIResistor::DrawSymbol(canvas, view, color, style);
    const ImVec2 tip = LocalToScreen(view, 0, ARROW_TIP_Y);
    canvas.AddLine(LocalToScreen(view, 0, -2), tip, color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -ARROW_HEAD_HALF_WIDTH, ARROW_TIP_Y - ARROW_HEAD_LENGTH), tip, color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, ARROW_HEAD_HALF_WIDTH, ARROW_TIP_Y - ARROW_HEAD_LENGTH), tip, color,
                   LINE_THICKNESS);
}

// One line with the name, the value and the position of the wiper, such as "RV1 10k 25%", so that the parts of the
// label never overlap whichever way the part is turned
void UIPotentiometer::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &pot = static_cast<const Core::Potentiometer &>(GetComponent());
    DrawLabel(canvas, view, {0.0f, LABEL_OFFSET_Y}, {0.0f, 1.0f},
              std::format("{} {} {:.0f}%", pot.GetName(), Core::FormatValue(pot.GetValue()), pot.GetWiper() * 100.0),
              color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  The ends at (-2, 0) and (2, 0), then the wiper at (0, -2).
 */
std::vector<GridPoint> UIPotentiometer::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}, {0, -2}};
}

/**
 * @brief   Returns the pick area of the potentiometer, in local grid units before rotation.
 * @return  A box around the body, the end leads and the wiper lead.
 */
LocalBounds UIPotentiometer::GetLocalBounds() const {
    return {{-2.0f, -2.0f}, {2.0f, 0.8f}};
}

} // namespace GUI
