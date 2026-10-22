/**
 * @file    ui_ground.cpp
 * @brief   Schematic symbol of a ground reference.
 */

#include "ui_ground.h"

#include "components/ground.h"
#include <memory>

namespace GUI {

namespace {

constexpr float LEAD_LENGTH = 1.0f;
constexpr float BAR_SPACING = 0.3f;
constexpr float BAR_HALF_WIDTHS[] = {0.8f, 0.5f, 0.2f};

} // namespace

/**
 * @brief   Creates a ground reference placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIGround::UIGround(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Ground>(), position, rotation) {
}

void UIGround::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, LEAD_LENGTH), color, LINE_THICKNESS);
}

void UIGround::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                          SymbolStyle /*style*/) const {
    float y = LEAD_LENGTH;
    for (const float half_width : BAR_HALF_WIDTHS) {
        canvas.AddLine(LocalToScreen(view, -half_width, y), LocalToScreen(view, half_width, y), color, LINE_THICKNESS);
        y += BAR_SPACING;
    }
}

// Ground has no name or value to show
void UIGround::DrawLabels(SchematicCanvas & /*canvas*/, const ViewTransform & /*view*/, ImU32 /*color*/) const {
}

std::vector<GridPoint> UIGround::GetLocalTerminals() const {
    return {{0, 0}};
}

LocalBounds UIGround::GetLocalBounds() const {
    return {{-0.8f, 0.0f}, {0.8f, 1.6f}};
}

} // namespace GUI
