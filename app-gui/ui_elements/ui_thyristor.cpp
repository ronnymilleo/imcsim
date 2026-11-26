/**
 * @file    ui_thyristor.cpp
 * @brief   Schematic symbol of a thyristor (SCR) or a TRIAC.
 */

#include "ui_elements/ui_thyristor.h"

#include "circuit/components/thyristor.h"
#include "spice/spice_value.h"
#include <format>
#include <memory>

namespace GUI {

namespace {

/// The triangle spans x = -0.5 to 0.5 and the bar sits at its tip, as a diode.
constexpr float BODY_HALF_LENGTH = 0.5f;
constexpr float BODY_HALF_HEIGHT = 0.6f;
/// Where the gate lead leaves the cathode bar, and the terminal it reaches below the symbol.
constexpr float GATE_LEAD_X = BODY_HALF_LENGTH;
constexpr float GATE_LEAD_Y = BODY_HALF_HEIGHT;
/// A TRIAC draws two anti-parallel triangles, their tips this far off the axis and their bases this tall.
constexpr float TRIAC_TIP_OFFSET = 0.25f;
constexpr float TRIAC_BASE_HALF_HEIGHT = 0.35f;
/// Labels sit just outside the symbol.
constexpr float LABEL_OFFSET = 1.0f;

} // namespace

/**
 * @brief   Creates a thyristor part placed on the grid, together with its simulation component.
 * @param[in] type      Core::ComponentType::THYRISTOR for an SCR, TRIAC for a bidirectional one.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIThyristor::UIThyristor(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Thyristor>(type), position, rotation) {
}

/**
 * @brief   The anode (MT2) and cathode (MT1) leads run along the axis; the gate lead runs from the cathode bar down to
 *          its terminal.
 */
void UIThyristor::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, -2, 0), LocalToScreen(view, -1, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 1, 0), LocalToScreen(view, 2, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, GATE_LEAD_X, GATE_LEAD_Y), LocalToScreen(view, 0, 2), color, LINE_THICKNESS);
}

void UIThyristor::DrawSymbol(SchematicCanvas &canvas,
                             const ViewTransform &view,
                             const ImU32 color,
                             SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, -BODY_HALF_LENGTH, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, 0), LocalToScreen(view, 1, 0), color, LINE_THICKNESS);
    if (GetComponent().GetType() == Core::ComponentType::TRIAC) {
        // Two anti-parallel triangles, one each way, sharing the MT1 bar: the upper conducts MT2 to MT1, the lower
        // MT1 to MT2
        canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET - TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET + TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, -TRIAC_TIP_OFFSET),
                           color,
                           LINE_THICKNESS);
        canvas.AddTriangle(LocalToScreen(view, BODY_HALF_LENGTH, TRIAC_TIP_OFFSET - TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, TRIAC_TIP_OFFSET + TRIAC_BASE_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, TRIAC_TIP_OFFSET),
                           color,
                           LINE_THICKNESS);
    } else {
        canvas.AddTriangle(LocalToScreen(view, -BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                           LocalToScreen(view, -BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                           LocalToScreen(view, BODY_HALF_LENGTH, 0),
                           color,
                           LINE_THICKNESS);
    }
    canvas.AddLine(LocalToScreen(view, BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                   LocalToScreen(view, BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                   color,
                   LINE_THICKNESS);
}

/**
 * @brief   Draws the thyristor name above the symbol and its holding current below, in local orientation.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Text color.
 */
void UIThyristor::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &thyristor = static_cast<const Core::Thyristor &>(GetComponent());
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, thyristor.GetName(), color);
    DrawLabel(canvas,
              view,
              {0.0f, LABEL_OFFSET},
              {0.0f, 1.0f},
              std::format("IH {}", Core::FormatValue(thyristor.GetParameters().HoldingCurrent)),
              color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Anode (MT2) at (-2, 0), cathode (MT1) at (2, 0) and gate at (0, 2).
 */
std::vector<GridPoint> UIThyristor::GetLocalTerminals() const {
    return {{-2, 0}, {2, 0}, {0, 2}};
}

/**
 * @brief   Returns the pick area of the part, in local grid units before rotation.
 * @return  The two-terminal box, as tall below as the gate lead.
 */
LocalBounds UIThyristor::GetLocalBounds() const {
    return {{-2.0f, -0.8f}, {2.0f, 2.0f}};
}

} // namespace GUI