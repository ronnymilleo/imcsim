/**
 * @file    ui_ground.cpp
 * @brief   Schematic symbol of a ground reference.
 */

#include "ui_ground.h"

#include "components/ground.h"
#include <memory>

namespace GUI {

namespace {

constexpr float LeadLength = 1.0f;
constexpr float BarSpacing = 0.3f;
constexpr float BarHalfWidths[] = {0.8f, 0.5f, 0.2f};

} // namespace

/**
 * @brief   Creates a ground reference placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIGround::UIGround(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Ground>(), position, rotation) {
}

void UIGround::DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, LeadLength), color, LineThickness);
}

std::vector<GridPoint> UIGround::GetLocalTerminals() const {
    return {{0, 0}};
}

LocalBounds UIGround::GetLocalBounds() const {
    return {{-0.8f, 0.0f}, {0.8f, 1.6f}};
}

// Ground has no name or value to show
void UIGround::DrawLabels(ImDrawList * /*draw_list*/, const ViewTransform & /*view*/, ImU32 /*color*/) const {
}

void UIGround::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                          SymbolStyle /*style*/) const {
    float y = LeadLength;
    for (const float half_width : BarHalfWidths) {
        draw_list->AddLine(LocalToScreen(view, -half_width, y), LocalToScreen(view, half_width, y), color,
                           LineThickness);
        y += BarSpacing;
    }
}

} // namespace GUI
