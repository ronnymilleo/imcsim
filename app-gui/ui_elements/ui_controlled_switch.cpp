/**
 * @file    ui_controlled_switch.cpp
 * @brief   Schematic symbol of a voltage-controlled switch and of a relay.
 */

#include "ui_controlled_switch.h"

#include "components/relay.h"
#include "components/voltage_controlled_switch.h"
#include "spice_value.h"
#include <cmath>
#include <format>
#include <memory>

namespace GUI {

namespace {

// The contact: two points on the vertical axis, and a lever from the lower one that rests above the upper one
constexpr float CONTACT_Y = 0.7f;
constexpr float CONTACT_RADIUS = 0.12f;
constexpr ImVec2 LEVER_TIP = {0.7f, -0.5f};
// The control leads end at a bar, or at the coil, which is a box this wide and tall around (-1.2, 0)
constexpr float CONTROL_END_X = -1.2f;
constexpr float BAR_HALF_LENGTH = 1.0f;
constexpr float COIL_HALF_WIDTH = 0.4f;
constexpr float COIL_HALF_HEIGHT = 0.8f;
constexpr float SIGN_HALF_SIZE = 0.15f;
constexpr ImVec2 CONTROL_PLUS_SIGN = {-1.7f, -1.4f};
constexpr ImVec2 CONTROL_MINUS_SIGN = {-1.7f, 1.4f};
constexpr float DASH_LENGTH = 0.2f;
constexpr float DASH_GAP = 0.15f;
// Where the dashed line reaches the lever
constexpr float LINK_END_X = 0.25f;
constexpr float LABEL_OFFSET_X = 1.1f;
constexpr float LABEL_OFFSET_Y = 0.5f;

} // namespace

/**
 * @brief   Creates a voltage-controlled switch or a relay placed on the grid, with its simulation component.
 * @param[in] type      ComponentType::VSwitch or ComponentType::Relay.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIControlledSwitch::UIControlledSwitch(const Core::ComponentType type, const GridPoint position,
                                       const Rotation rotation)
    : UIElement(type == Core::ComponentType::RELAY
                    ? std::unique_ptr<Core::Component>(std::make_unique<Core::Relay>())
                    : std::unique_ptr<Core::Component>(std::make_unique<Core::VoltageControlledSwitch>()),
                position, rotation) {
}

// The contact leads stop at the contacts; the control leads run to the bar or the coil, with their polarity beside
void UIControlledSwitch::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -CONTACT_Y - CONTACT_RADIUS), color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, CONTACT_Y + CONTACT_RADIUS), LocalToScreen(view, 0, 2), color,
                   LINE_THICKNESS);
    const float lead_end = IsRelay() ? -COIL_HALF_HEIGHT : -1.0f;
    canvas.AddLine(LocalToScreen(view, -2, -1), LocalToScreen(view, CONTROL_END_X, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -2, 1), LocalToScreen(view, CONTROL_END_X, 1), color, LINE_THICKNESS);
    if (IsRelay()) {
        canvas.AddLine(LocalToScreen(view, CONTROL_END_X, -1), LocalToScreen(view, CONTROL_END_X, lead_end), color,
                       LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, CONTROL_END_X, 1), LocalToScreen(view, CONTROL_END_X, -lead_end), color,
                       LINE_THICKNESS);
    }
    DrawSign(canvas, view, CONTROL_PLUS_SIGN, true, color);
    DrawSign(canvas, view, CONTROL_MINUS_SIGN, false, color);
}

void UIControlledSwitch::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                    const SymbolStyle /*style*/) const {
    // The contact, drawn open
    const ImVec2 origin = LocalToScreen(view, 0, 0);
    const ImVec2 unit = LocalToScreen(view, 1, 0) - origin;
    const float radius = std::hypot(unit.x, unit.y) * CONTACT_RADIUS;
    canvas.AddCircle(LocalToScreen(view, 0, -CONTACT_Y), radius, color, 0, LINE_THICKNESS);
    canvas.AddCircle(LocalToScreen(view, 0, CONTACT_Y), radius, color, 0, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, 0, CONTACT_Y - CONTACT_RADIUS), LocalToScreen(view, LEVER_TIP.x, LEVER_TIP.y),
                   color, LINE_THICKNESS);

    if (IsRelay()) {
        const ImVec2 coil[4] = {LocalToScreen(view, CONTROL_END_X - COIL_HALF_WIDTH, -COIL_HALF_HEIGHT),
                                LocalToScreen(view, CONTROL_END_X + COIL_HALF_WIDTH, -COIL_HALF_HEIGHT),
                                LocalToScreen(view, CONTROL_END_X + COIL_HALF_WIDTH, COIL_HALF_HEIGHT),
                                LocalToScreen(view, CONTROL_END_X - COIL_HALF_WIDTH, COIL_HALF_HEIGHT)};
        canvas.AddPolyline(coil, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
        DrawDashedLine(canvas, view, CONTROL_END_X + COIL_HALF_WIDTH, LINK_END_X, 0.0f, color);
        return;
    }
    canvas.AddLine(LocalToScreen(view, CONTROL_END_X, -1), LocalToScreen(view, CONTROL_END_X, 1), color,
                   LINE_THICKNESS);
    DrawDashedLine(canvas, view, CONTROL_END_X, LINK_END_X, 0.0f, color);
}

// The threshold of a switch, or the pull-in voltage of a relay, under the name
void UIControlledSwitch::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const double voltage =
        IsRelay() ? static_cast<const Core::Relay &>(GetComponent()).GetParameters().PullInVoltage
                  : static_cast<const Core::VoltageControlledSwitch &>(GetComponent()).GetParameters().Threshold;
    DrawLabel(canvas, view, {LABEL_OFFSET_X, -LABEL_OFFSET_Y}, {1.0f, 0.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {LABEL_OFFSET_X, LABEL_OFFSET_Y}, {1.0f, 0.0f},
              std::format("{}{}V", IsRelay() ? "pull-in " : "Vt ", Core::FormatValue(voltage)), color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Contact terminals 1 at (0, -2) and 2 at (0, 2), then control + at (-2, -1) and control - at (-2, 1).
 */
std::vector<GridPoint> UIControlledSwitch::GetLocalTerminals() const {
    return {{0, -2}, {0, 2}, {-2, -1}, {-2, 1}};
}

/**
 * @brief   Returns the pick area of the part, in local grid units before rotation.
 * @return  A box from the control pins to the lever, as tall as the contact leads.
 */
LocalBounds UIControlledSwitch::GetLocalBounds() const {
    return {{-2.0f, -2.0f}, {LEVER_TIP.x + 0.2f, 2.0f}};
}

bool UIControlledSwitch::IsRelay() const {
    return GetComponent().GetType() == Core::ComponentType::RELAY;
}

void UIControlledSwitch::DrawSign(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 center,
                                  const bool plus, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, center.x - SIGN_HALF_SIZE, center.y),
                   LocalToScreen(view, center.x + SIGN_HALF_SIZE, center.y), color, LINE_THICKNESS);
    if (plus) {
        canvas.AddLine(LocalToScreen(view, center.x, center.y - SIGN_HALF_SIZE),
                       LocalToScreen(view, center.x, center.y + SIGN_HALF_SIZE), color, LINE_THICKNESS);
    }
}

void UIControlledSwitch::DrawDashedLine(SchematicCanvas &canvas, const ViewTransform &view, const float from_x,
                                        const float to_x, const float y, const ImU32 color) const {
    for (float x = from_x + DASH_GAP; x + DASH_LENGTH <= to_x; x += DASH_LENGTH + DASH_GAP) {
        canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, x + DASH_LENGTH, y), color, LINE_THICKNESS);
    }
}

} // namespace GUI
