/**
 * @file    ui_net_label.cpp
 * @brief   Schematic symbol of a net label.
 */

#include "ui_net_label.h"

#include "components/net_label.h"
#include <cmath>
#include <memory>
#include <string>

namespace GUI {

namespace {

// The lead ends at the foot of a small circle, which no other symbol uses on a lead, so a label is not taken for a
// supply rail
constexpr float LEAD_LENGTH = 0.7f;
constexpr float CIRCLE_RADIUS = 0.4f;
constexpr float CIRCLE_TOP = LEAD_LENGTH + 2.0f * CIRCLE_RADIUS;
// The name starts a little past the top of the circle
constexpr float LABEL_OFFSET = CIRCLE_TOP + 0.2f;

} // namespace

/**
 * @brief   Creates a net label placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UINetLabel::UINetLabel(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::NetLabel>(), position, rotation) {
}

void UINetLabel::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LEAD_LENGTH), color, LINE_THICKNESS);
}

void UINetLabel::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                            SymbolStyle /*style*/) const {
    const ImVec2 center = LocalToScreen(view, 0, -(LEAD_LENGTH + CIRCLE_RADIUS));
    const ImVec2 radius_offset = LocalToScreen(view, CIRCLE_RADIUS, -(LEAD_LENGTH + CIRCLE_RADIUS)) - center;
    canvas.AddCircle(center, std::hypot(radius_offset.x, radius_offset.y), color, 0, LINE_THICKNESS);
}

// The name of the net stands where parts show their name; a label with none shows a question mark
void UINetLabel::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string &net = static_cast<const Core::NetLabel &>(GetComponent()).GetNet();
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, net.empty() ? "?" : net, color);
}

std::vector<GridPoint> UINetLabel::GetLocalTerminals() const {
    return {{0, 0}};
}

// The terminal is at the foot of the symbol, and its lead goes up to the circle
GridPoint UINetLabel::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UINetLabel::GetLocalBounds() const {
    return {{-CIRCLE_RADIUS, -CIRCLE_TOP}, {CIRCLE_RADIUS, 0.0f}};
}

} // namespace GUI
