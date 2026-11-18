/**
 * @file    ui_initial_voltage.cpp
 * @brief   Schematic symbol of an initial voltage marker.
 */

#include "ui_elements/ui_initial_voltage.h"

#include "circuit/components/initial_voltage.h"
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
/// The text starts a little past the top of the square.
constexpr float LABEL_OFFSET = -BOX_TOP + 0.2f;

} // namespace

/**
 * @brief   Creates an initial voltage marker placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIInitialVoltage::UIInitialVoltage(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::InitialVoltage>(), position, rotation) {
}

void UIInitialVoltage::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LEAD_LENGTH), color, LINE_THICKNESS);
}

void UIInitialVoltage::DrawSymbol(SchematicCanvas &canvas,
                                  const ViewTransform &view,
                                  const ImU32 color,
                                  SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {LocalToScreen(view, -BOX_HALF_WIDTH, BOX_TOP),
                                       LocalToScreen(view, BOX_HALF_WIDTH, BOX_TOP),
                                       LocalToScreen(view, BOX_HALF_WIDTH, -LEAD_LENGTH),
                                       LocalToScreen(view, -BOX_HALF_WIDTH, -LEAD_LENGTH)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

/**
 * @brief   The role and the voltage, such as "IC 2.5V", stand past the square; the part has no name.
 */
void UIInitialVoltage::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string label = std::format("IC {}V", Core::FormatValue(GetComponent().GetValue()));
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, label, color);
}

std::vector<GridPoint> UIInitialVoltage::GetLocalTerminals() const {
    return {{0, 0}};
}

/**
 * @brief   The terminal is at the foot of the symbol, and its lead goes up to the square.
 */
GridPoint UIInitialVoltage::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UIInitialVoltage::GetLocalBounds() const {
    return {{-BOX_HALF_WIDTH, BOX_TOP}, {BOX_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
