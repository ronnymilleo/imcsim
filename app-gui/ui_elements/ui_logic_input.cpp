/**
 * @file    ui_logic_input.cpp
 * @brief   Schematic symbol of a logic input that drives a circuit high or low.
 */

#include "ui_logic_input.h"

#include "components/logic_input.h"
#include <array>

namespace GUI {

namespace {

constexpr float LeadLength = 0.6f;
constexpr float BoxHalfWidth = 0.7f;
constexpr float BoxTop = -1.7f;
constexpr float BoxBottom = -LeadLength;
constexpr float LabelOffset = 0.3f;

} // namespace

/**
 * @brief   Creates a logic input placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of the terminal, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UILogicInput::UILogicInput(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::LogicInput>(), position, rotation) {
}

void UILogicInput::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    canvas.AddLine(LocalToScreen(view, 0, 0), LocalToScreen(view, 0, BoxBottom), color, LineThickness);
}

// The box shows the level the part drives, 1 when high and 0 when low
void UILogicInput::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                              const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {
        LocalToScreen(view, -BoxHalfWidth, BoxTop), LocalToScreen(view, BoxHalfWidth, BoxTop),
        LocalToScreen(view, BoxHalfWidth, BoxBottom), LocalToScreen(view, -BoxHalfWidth, BoxBottom)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LineThickness);
    const bool high = static_cast<const Core::LogicInput &>(GetComponent()).IsHigh();
    DrawLabel(canvas, view, {0.0f, (BoxTop + BoxBottom) / 2.0f}, {0.0f, 1.0f}, high ? "1" : "0", color);
}

// The name sits above the box
void UILogicInput::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(canvas, view, {0.0f, BoxTop - LabelOffset}, {0.0f, -1.0f}, GetComponent().GetName(), color);
}

std::vector<GridPoint> UILogicInput::GetLocalTerminals() const {
    return {{0, 0}};
}

// The terminal is at the origin and its lead goes up to the box
GridPoint UILogicInput::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {0, -1};
}

LocalBounds UILogicInput::GetLocalBounds() const {
    return {{-BoxHalfWidth, BoxTop}, {BoxHalfWidth, 0.0f}};
}

} // namespace GUI
