/**
 * @file    ui_vcc.cpp
 * @brief   Schematic symbol of a VCC supply rail.
 */

#include "ui_vcc.h"

#include "components/vcc.h"
#include "spice_value.h"
#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

constexpr float LeadLength = 1.0f;
constexpr float BarHalfWidth = 0.6f;
// The label starts a little past the bar
constexpr float LabelOffset = 1.2f;

} // namespace

/**
 * @brief   Creates a VCC supply rail placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVCC::UIVCC(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::VCC>(), position, rotation) {
}

void UIVCC::DrawTerminals(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    draw_list->AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LeadLength), color, LineThickness);
}

void UIVCC::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                       SymbolStyle /*style*/) const {
    draw_list->AddLine(LocalToScreen(view, -BarHalfWidth, -LeadLength), LocalToScreen(view, BarHalfWidth, -LeadLength),
                       color, LineThickness);
}

// The rail is labeled by its role and voltage past the bar; the SPICE name (V1) only matters in the netlist
void UIVCC::DrawLabels(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const std::string label = std::format("VCC {}V", Core::FormatValue(GetComponent().GetValue()));
    DrawLabel(draw_list, view, {0.0f, -LabelOffset}, {0.0f, -1.0f}, label, color);
}

std::vector<GridPoint> UIVCC::GetLocalTerminals() const {
    return {{0, 0}};
}

LocalBounds UIVCC::GetLocalBounds() const {
    return {{-0.6f, -1.0f}, {0.6f, 0.0f}};
}

} // namespace GUI
