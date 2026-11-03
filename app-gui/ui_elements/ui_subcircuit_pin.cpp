/**
 * @file    ui_subcircuit_pin.cpp
 * @brief   Schematic symbols of explicit subcircuit input and output markers.
 */

#include "ui_subcircuit_pin.h"

#include "components/subcircuit_pin.h"
#include <array>
#include <memory>
#include <string>

namespace GUI {

namespace {

// The lead rises to a small square, the block side of the pin, with an arrow beside it
constexpr float LEAD_LENGTH = 0.7f;
constexpr float SQUARE_SIDE = 0.6f;
constexpr float ARROW_GAP = 0.05f;
constexpr float ARROW_LENGTH = 0.95f;
constexpr float HEAD_LENGTH = 0.3f;
constexpr float HEAD_HALF_WIDTH = 0.18f;
constexpr float LABEL_OFFSET = 2.1f;
constexpr float BODY_HALF_WIDTH = SQUARE_SIDE / 2.0f + ARROW_GAP + ARROW_LENGTH;

} // namespace

/**
 * @brief   Creates an unnamed marker.
 * @param[in] type      ComponentType::PIN or ComponentType::POUT.
 * @param[in] position  Grid point of its terminal.
 * @param[in] rotation  Orientation of the symbol.
 */
UISubcircuitPin::UISubcircuitPin(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::SubcircuitPinMarker>(type), position, rotation) {
}

void UISubcircuitPin::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, -LEAD_LENGTH), color, LINE_THICKNESS);
}

// An input is an arrow pointing into the square and an output an arrow leaving it, both the way the signal flows
void UISubcircuitPin::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 SymbolStyle /*style*/) const {
    const float half_side = SQUARE_SIDE / 2.0f;
    const float middle = -LEAD_LENGTH - half_side;
    const std::array square = {LocalToScreen(view, -half_side, -LEAD_LENGTH),
                               LocalToScreen(view, half_side, -LEAD_LENGTH),
                               LocalToScreen(view, half_side, -LEAD_LENGTH - SQUARE_SIDE),
                               LocalToScreen(view, -half_side, -LEAD_LENGTH - SQUARE_SIDE)};
    canvas.AddPolyline(square.data(), static_cast<int>(square.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);

    const bool is_input = GetComponent().GetType() == Core::ComponentType::PIN;
    const float tail = is_input ? -BODY_HALF_WIDTH : half_side + ARROW_GAP;
    const float tip = is_input ? -half_side - ARROW_GAP : BODY_HALF_WIDTH;
    const float head_base = tip - HEAD_LENGTH;
    canvas.AddLine(LocalToScreen(view, tail, middle), LocalToScreen(view, head_base, middle), color, LINE_THICKNESS);
    canvas.AddTriangleFilled(LocalToScreen(view, tip, middle), LocalToScreen(view, head_base, middle - HEAD_HALF_WIDTH),
                             LocalToScreen(view, head_base, middle + HEAD_HALF_WIDTH), color);
}

void UISubcircuitPin::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::string &name = static_cast<const Core::SubcircuitPinMarker &>(GetComponent()).GetNet();
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, name.empty() ? "?" : name, color);
}

std::vector<GridPoint> UISubcircuitPin::GetLocalTerminals() const {
    return {{0, 0}};
}

GridPoint UISubcircuitPin::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UISubcircuitPin::GetLocalBounds() const {
    return {{-BODY_HALF_WIDTH, -LEAD_LENGTH - SQUARE_SIDE - HEAD_HALF_WIDTH}, {BODY_HALF_WIDTH, 0.0f}};
}

} // namespace GUI
