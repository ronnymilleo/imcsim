/**
 * @file    ui_ground.cpp
 * @brief   Schematic symbol of a ground reference.
 */

#include "ui_elements/ui_ground.h"

#include "circuit/components/ground.h"
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
    : UISingleTerminalElement(std::make_unique<Core::Ground>(), position, rotation) {
}

void UIGround::DrawSymbol(SchematicCanvas &canvas,
                          const ViewTransform &view,
                          const ImU32 color,
                          SymbolStyle /*style*/) const {
    float y = LEAD_LENGTH;
    for (const float half_width : BAR_HALF_WIDTHS) {
        canvas.AddLine(LocalToScreen(view, -half_width, y), LocalToScreen(view, half_width, y), color, LINE_THICKNESS);
        y += BAR_SPACING;
    }
}

/**
 * @brief   Ground has no name or value to show.
 * @param[in,out] canvas  Where it would be drawn (unused).
 * @param[in]     view    Transform of the current frame (unused).
 * @param[in]     color   Text color (unused).
 */
void UIGround::DrawLabels([[maybe_unused]] SchematicCanvas &canvas,
                          [[maybe_unused]] const ViewTransform &view,
                          [[maybe_unused]] const ImU32 color) const {
}

LocalBounds UIGround::GetLocalBounds() const {
    return {{-0.8f, 0.0f}, {0.8f, 1.6f}};
}

/**
 * @brief   The lead goes down from the terminal to the bars.
 * @return  Top center of the ground bars in local grid units.
 */
ImVec2 UIGround::GetLeadEnd() const {
    return {0.0f, LEAD_LENGTH};
}

} // namespace GUI
