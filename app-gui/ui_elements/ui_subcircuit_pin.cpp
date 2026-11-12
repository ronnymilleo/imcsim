/**
 * @file    ui_subcircuit_pin.cpp
 * @brief   Schematic symbol of the pin markers that give a block its connections.
 */

#include "ui_elements/ui_subcircuit_pin.h"

#include "circuit/components/subcircuit_pin.h"
#include <array>
#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

/// Unrotated, the terminal is at the origin, the lead runs left from it to the square, and the arrow inside the square
/// points right, toward the terminal.
constexpr float LEAD_LENGTH = 0.5f;
constexpr float SQUARE_SIDE = 1.0f;
constexpr float ARROW_INSET = 0.15f;
constexpr float HEAD_LENGTH = 0.3f;
constexpr float HEAD_HALF_WIDTH = 0.2f;
constexpr float LABEL_GAP = 0.3f;
constexpr float SQUARE_NEAR = -LEAD_LENGTH;
constexpr float SQUARE_FAR = -LEAD_LENGTH - SQUARE_SIDE;
constexpr float HALF_SIDE = SQUARE_SIDE / 2.0f;

} // namespace

/**
 * @brief   Creates an unnamed marker.
 * @param[in] position  Grid point of its terminal.
 * @param[in] rotation  Orientation of the symbol, which decides the edge of the block the pin goes on.
 */
UISubcircuitPin::UISubcircuitPin(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::SubcircuitPinMarker>(), position, rotation) {
}

void UISubcircuitPin::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, SQUARE_NEAR, 0), color, LINE_THICKNESS);
}

void UISubcircuitPin::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 SymbolStyle /*style*/) const {
    const std::array square = {LocalToScreen(view, SQUARE_FAR, -HALF_SIDE),
                               LocalToScreen(view, SQUARE_NEAR, -HALF_SIDE),
                               LocalToScreen(view, SQUARE_NEAR, HALF_SIDE), LocalToScreen(view, SQUARE_FAR, HALF_SIDE)};
    canvas.AddPolyline(square.data(), static_cast<int>(square.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);

    const float tail = SQUARE_FAR + ARROW_INSET;
    const float tip = SQUARE_NEAR - ARROW_INSET;
    const float head_base = tip - HEAD_LENGTH;
    canvas.AddLine(LocalToScreen(view, tail, 0.0f), LocalToScreen(view, head_base, 0.0f), color, LINE_THICKNESS);
    canvas.AddTriangleFilled(LocalToScreen(view, tip, 0.0f), LocalToScreen(view, head_base, -HEAD_HALF_WIDTH),
                             LocalToScreen(view, head_base, HEAD_HALF_WIDTH), color);
}

/**
 * @brief   The name sits beyond the square, outside the circuit, where the block's edge is; a port reads its number
 *          first, such as "P1" or "P2 (OUT)".
 */
void UISubcircuitPin::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &marker = static_cast<const Core::SubcircuitPinMarker &>(GetComponent());
    const std::string &name = marker.GetNet();
    std::string text = name.empty() ? "?" : name;
    if (const Core::PortSettings &port = marker.GetPort(); port.IsPort) {
        text = name.empty() ? std::format("P{}", port.Number) : std::format("P{} ({})", port.Number, name);
    }
    DrawLabel(canvas, view, {SQUARE_FAR - LABEL_GAP, 0.0f}, {-1.0f, 0.0f}, text, color);
}

std::vector<GridPoint> UISubcircuitPin::GetLocalTerminals() const {
    return {{0, 0}};
}

GridPoint UISubcircuitPin::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {-1, 0};
}

LocalBounds UISubcircuitPin::GetLocalBounds() const {
    return {{SQUARE_FAR, -HALF_SIDE}, {0.0f, HALF_SIDE}};
}

} // namespace GUI
