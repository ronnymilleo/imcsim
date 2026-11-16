/**
 * @file    ui_nodeset.cpp
 * @brief   Schematic symbol of a nodeset marker.
 */

#include "ui_elements/ui_nodeset.h"

#include "circuit/components/nodeset.h"
#include "spice/spice_value.h"
#include <array>
#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

constexpr float LEAD_LENGTH = 0.6f;
constexpr float BOX_HALF_WIDTH = 0.45f;
constexpr float BOX_TOP = -(LEAD_LENGTH + 2.0f * BOX_HALF_WIDTH);
constexpr float BOX_CENTER = -(LEAD_LENGTH + BOX_HALF_WIDTH);
/// The text starts a little past the top of the diamond.
constexpr float LABEL_OFFSET = -BOX_TOP + 0.2f;

} // namespace

/**
 * @brief   Creates a nodeset marker placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UINodeset::UINodeset(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Nodeset>(), position, rotation) {
}

void UINodeset::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LEAD_LENGTH), color, LINE_THICKNESS);
}

void UINodeset::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                           SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> diamond = {
        LocalToScreen(view, 0.0f, BOX_TOP), LocalToScreen(view, BOX_HALF_WIDTH, BOX_CENTER),
        LocalToScreen(view, 0.0f, -LEAD_LENGTH), LocalToScreen(view, -BOX_HALF_WIDTH, BOX_CENTER)};
    canvas.AddPolyline(diamond.data(), static_cast<int>(diamond.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The role and the voltage, such as "NS 2.5V", stand past the diamond; the part has no name.
 */
void UINodeset::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string label = std::format("NS {}V", Core::FormatValue(GetComponent().GetValue()));
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, label, color);
}

std::vector<GridPoint> UINodeset::GetLocalTerminals() const {
    return {{0, 0}};
}

/**
 * @brief   The terminal is at the foot of the symbol, and its lead goes up to the diamond.
 */
GridPoint UINodeset::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UINodeset::GetLocalBounds() const {
    return {{-BOX_HALF_WIDTH, BOX_TOP}, {BOX_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
