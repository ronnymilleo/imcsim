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
constexpr float ContactY = 0.7f;
constexpr float ContactRadius = 0.12f;
constexpr ImVec2 LeverTip = {0.7f, -0.5f};
// The control leads end at a bar, or at the coil, which is a box this wide and tall around (-1.2, 0)
constexpr float ControlEndX = -1.2f;
constexpr float BarHalfLength = 1.0f;
constexpr float CoilHalfWidth = 0.4f;
constexpr float CoilHalfHeight = 0.8f;
constexpr float SignHalfSize = 0.15f;
constexpr ImVec2 ControlPlusSign = {-1.7f, -1.4f};
constexpr ImVec2 ControlMinusSign = {-1.7f, 1.4f};
constexpr float DashLength = 0.2f;
constexpr float DashGap = 0.15f;
// Where the dashed line reaches the lever
constexpr float LinkEndX = 0.25f;
constexpr float LabelOffsetX = 1.1f;
constexpr float LabelOffsetY = 0.5f;

} // namespace

/**
 * @brief   Creates a voltage-controlled switch or a relay placed on the grid, with its simulation component.
 * @param[in] type      ComponentType::VSwitch or ComponentType::Relay.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIControlledSwitch::UIControlledSwitch(const Core::ComponentType type, const GridPoint position,
                                       const Rotation rotation)
    : UIElement(type == Core::ComponentType::Relay
                    ? std::unique_ptr<Core::Component>(std::make_unique<Core::Relay>())
                    : std::unique_ptr<Core::Component>(std::make_unique<Core::VoltageControlledSwitch>()),
                position, rotation) {
}

// The contact leads stop at the contacts; the control leads run to the bar or the coil, with their polarity beside
void UIControlledSwitch::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, -2), LocalToScreen(view, 0, -ContactY - ContactRadius), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, 0, ContactY + ContactRadius), LocalToScreen(view, 0, 2), color, LineThickness);
    const float lead_end = IsRelay() ? -CoilHalfHeight : -1.0f;
    canvas.AddLine(LocalToScreen(view, -2, -1), LocalToScreen(view, ControlEndX, -1), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, -2, 1), LocalToScreen(view, ControlEndX, 1), color, LineThickness);
    if (IsRelay()) {
        canvas.AddLine(LocalToScreen(view, ControlEndX, -1), LocalToScreen(view, ControlEndX, lead_end), color,
                       LineThickness);
        canvas.AddLine(LocalToScreen(view, ControlEndX, 1), LocalToScreen(view, ControlEndX, -lead_end), color,
                       LineThickness);
    }
    DrawSign(canvas, view, ControlPlusSign, true, color);
    DrawSign(canvas, view, ControlMinusSign, false, color);
}

void UIControlledSwitch::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                    const SymbolStyle /*style*/) const {
    // The contact, drawn open
    const ImVec2 origin = LocalToScreen(view, 0, 0);
    const ImVec2 unit = LocalToScreen(view, 1, 0) - origin;
    const float radius = std::hypot(unit.x, unit.y) * ContactRadius;
    canvas.AddCircle(LocalToScreen(view, 0, -ContactY), radius, color, 0, LineThickness);
    canvas.AddCircle(LocalToScreen(view, 0, ContactY), radius, color, 0, LineThickness);
    canvas.AddLine(LocalToScreen(view, 0, ContactY - ContactRadius), LocalToScreen(view, LeverTip.x, LeverTip.y), color,
                   LineThickness);

    if (IsRelay()) {
        const ImVec2 coil[4] = {LocalToScreen(view, ControlEndX - CoilHalfWidth, -CoilHalfHeight),
                                LocalToScreen(view, ControlEndX + CoilHalfWidth, -CoilHalfHeight),
                                LocalToScreen(view, ControlEndX + CoilHalfWidth, CoilHalfHeight),
                                LocalToScreen(view, ControlEndX - CoilHalfWidth, CoilHalfHeight)};
        canvas.AddPolyline(coil, 4, color, ImDrawFlags_Closed, LineThickness);
        DrawDashedLine(canvas, view, ControlEndX + CoilHalfWidth, LinkEndX, 0.0f, color);
        return;
    }
    canvas.AddLine(LocalToScreen(view, ControlEndX, -1), LocalToScreen(view, ControlEndX, 1), color, LineThickness);
    DrawDashedLine(canvas, view, ControlEndX, LinkEndX, 0.0f, color);
}

// The threshold of a switch, or the pull-in voltage of a relay, under the name
void UIControlledSwitch::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const double voltage =
        IsRelay() ? static_cast<const Core::Relay &>(GetComponent()).GetParameters().PullInVoltage
                  : static_cast<const Core::VoltageControlledSwitch &>(GetComponent()).GetParameters().Threshold;
    DrawLabel(canvas, view, {LabelOffsetX, -LabelOffsetY}, {1.0f, 0.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {LabelOffsetX, LabelOffsetY}, {1.0f, 0.0f},
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
    return {{-2.0f, -2.0f}, {LeverTip.x + 0.2f, 2.0f}};
}

bool UIControlledSwitch::IsRelay() const {
    return GetComponent().GetType() == Core::ComponentType::Relay;
}

void UIControlledSwitch::DrawSign(SchematicCanvas &canvas, const ViewTransform &view, const ImVec2 center,
                                  const bool plus, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, center.x - SignHalfSize, center.y),
                   LocalToScreen(view, center.x + SignHalfSize, center.y), color, LineThickness);
    if (plus) {
        canvas.AddLine(LocalToScreen(view, center.x, center.y - SignHalfSize),
                       LocalToScreen(view, center.x, center.y + SignHalfSize), color, LineThickness);
    }
}

void UIControlledSwitch::DrawDashedLine(SchematicCanvas &canvas, const ViewTransform &view, const float from_x,
                                        const float to_x, const float y, const ImU32 color) const {
    for (float x = from_x + DashGap; x + DashLength <= to_x; x += DashLength + DashGap) {
        canvas.AddLine(LocalToScreen(view, x, y), LocalToScreen(view, x + DashLength, y), color, LineThickness);
    }
}

} // namespace GUI
