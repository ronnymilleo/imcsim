/**
 * @file    ui_subcircuit_pin.cpp
 * @brief   Schematic symbols of explicit subcircuit input and output markers.
 */

#include "ui_subcircuit_pin.h"

#include "components/subcircuit_pin.h"
#include <memory>
#include <string>

namespace GUI {

namespace {

constexpr float LEAD_LENGTH = 0.7f;
constexpr float BODY_HEIGHT = 0.8f;
constexpr float BODY_WIDTH = 1.0f;
constexpr float LABEL_OFFSET = 2.1f;

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

void UISubcircuitPin::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 SymbolStyle /*style*/) const {
    const float direction = GetComponent().GetType() == Core::ComponentType::PIN ? 1.0f : -1.0f;
    const float left = direction > 0.0f ? -BODY_WIDTH / 2.0f : -BODY_WIDTH / 2.0f - 0.35f;
    const float right = direction > 0.0f ? BODY_WIDTH / 2.0f + 0.35f : BODY_WIDTH / 2.0f;
    canvas.AddLine(LocalToScreen(view, left, -LEAD_LENGTH - BODY_HEIGHT / 2.0f),
                   LocalToScreen(view, right, -LEAD_LENGTH), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, right, -LEAD_LENGTH),
                   LocalToScreen(view, left, -LEAD_LENGTH + BODY_HEIGHT / 2.0f), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, left, -LEAD_LENGTH + BODY_HEIGHT / 2.0f),
                   LocalToScreen(view, left, -LEAD_LENGTH - BODY_HEIGHT / 2.0f), color, LINE_THICKNESS);
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
    return {{-0.85f, -1.5f}, {0.85f, 0.0f}};
}

} // namespace GUI
