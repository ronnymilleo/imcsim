/**
 * @file    ui_net_label.cpp
 * @brief   Schematic symbol of a net label.
 */

#include "ui_elements/ui_net_label.h"

#include "circuit/components/net_label.h"
#include <memory>
#include <string>

namespace GUI {

namespace {

/// The lead ends at the foot of a small circle, which no other symbol uses on a lead, so a label is not taken for a
/// supply rail.
constexpr float LEAD_LENGTH = 0.7f;
constexpr float CIRCLE_RADIUS = 0.4f;
constexpr float CIRCLE_TOP = LEAD_LENGTH + 2.0f * CIRCLE_RADIUS;
/// The name starts a little past the top of the circle.
constexpr float LABEL_OFFSET = CIRCLE_TOP + 0.2f;

} // namespace

/**
 * @brief   Creates a net label placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UINetLabel::UINetLabel(const GridPoint position, const Rotation rotation)
    : UISingleTerminalElement(std::make_unique<Core::NetLabel>(), position, rotation) {
}

void UINetLabel::DrawSymbol(SchematicCanvas &canvas,
                            const ViewTransform &view,
                            const ImU32 color,
                            SymbolStyle /*style*/) const {
    DrawCircleAt(canvas, view, {0.0f, -(LEAD_LENGTH + CIRCLE_RADIUS)}, CIRCLE_RADIUS, color);
}

/**
 * @brief   The name of the net stands where parts show their name; a label with none shows a question mark.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UINetLabel::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string &net = static_cast<const Core::NetLabel &>(GetComponent()).GetNet();
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, net.empty() ? "?" : net, color);
}

LocalBounds UINetLabel::GetLocalBounds() const {
    return {{-CIRCLE_RADIUS, -CIRCLE_TOP}, {CIRCLE_RADIUS, 0.0f}};
}

/**
 * @brief   The terminal is at the foot of the symbol, and its lead goes up to the circle.
 * @return  Bottom of the net-label circle in local grid units.
 */
ImVec2 UINetLabel::GetLeadEnd() const {
    return {0.0f, -LEAD_LENGTH};
}

} // namespace GUI
