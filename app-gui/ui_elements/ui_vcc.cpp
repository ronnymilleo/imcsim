/**
 * @file    ui_vcc.cpp
 * @brief   Schematic symbol of a VCC supply rail.
 */

#include "ui_elements/ui_vcc.h"

#include "circuit/components/vcc.h"
#include "spice/spice_value.h"
#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

constexpr float LEAD_LENGTH = 1.0f;
constexpr float BAR_HALF_WIDTH = 0.6f;
/// The label starts a little past the bar.
constexpr float LABEL_OFFSET = 1.2f;

} // namespace

/**
 * @brief   Creates a VCC supply rail placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVCC::UIVCC(const GridPoint position, const Rotation rotation)
    : UISingleTerminalElement(std::make_unique<Core::VCC>(), position, rotation) {
}

void UIVCC::DrawSymbol(SchematicCanvas &canvas,
                       const ViewTransform &view,
                       const ImU32 color,
                       SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -BAR_HALF_WIDTH, -LEAD_LENGTH),
                   LocalToScreen(view, BAR_HALF_WIDTH, -LEAD_LENGTH),
                   color,
                   LINE_THICKNESS);
}

/**
 * @brief   The rail is labeled by its role and voltage past the bar; the SPICE name (V1) only matters in the netlist.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UIVCC::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string label = std::format("VCC {}V", Core::FormatValue(GetComponent().GetValue()));
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, label, color);
}

LocalBounds UIVCC::GetLocalBounds() const {
    return {{-0.6f, -1.0f}, {0.6f, 0.0f}};
}

/**
 * @brief   The terminal is at the middle of the symbol, and its lead goes up to the bar.
 * @return  Center of the supply rail bar in local grid units.
 */
ImVec2 UIVCC::GetLeadEnd() const {
    return {0.0f, -LEAD_LENGTH};
}

} // namespace GUI
