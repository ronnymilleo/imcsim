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

constexpr float LEAD_LENGTH = 1.0f;
constexpr float BAR_HALF_WIDTH = 0.6f;
// The label starts a little past the bar
constexpr float LABEL_OFFSET = 1.2f;

} // namespace

/**
 * @brief   Creates a VCC supply rail placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVCC::UIVCC(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::VCC>(), position, rotation) {
}

void UIVCC::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LEAD_LENGTH), color, LINE_THICKNESS);
}

void UIVCC::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                       SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -BAR_HALF_WIDTH, -LEAD_LENGTH),
                   LocalToScreen(view, BAR_HALF_WIDTH, -LEAD_LENGTH), color, LINE_THICKNESS);
}

// The rail is labeled by its role and voltage past the bar; the SPICE name (V1) only matters in the netlist
void UIVCC::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string label = std::format("VCC {}V", Core::FormatValue(GetComponent().GetValue()));
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, label, color);
}

std::vector<GridPoint> UIVCC::GetLocalTerminals() const {
    return {{0, 0}};
}

// The terminal is at the middle of the symbol, and its lead goes up to the bar
GridPoint UIVCC::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UIVCC::GetLocalBounds() const {
    return {{-0.6f, -1.0f}, {0.6f, 0.0f}};
}

} // namespace GUI
