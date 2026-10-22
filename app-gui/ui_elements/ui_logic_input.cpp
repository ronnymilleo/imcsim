/**
 * @file    ui_logic_input.cpp
 * @brief   Schematic symbol of a logic input that drives a circuit high or low.
 */

#include "ui_logic_input.h"

#include "components/logic_input.h"
#include <array>
#include <cfloat>
#include <cmath>

namespace GUI {

namespace {

constexpr float LeadLength = 0.6f;
constexpr float BoxHalfWidth = 0.8f;
constexpr float BoxTop = -2.0f;
constexpr float BoxBottom = -LeadLength;
constexpr float LabelOffset = 0.3f;
// The digit is the state of the part, so it is larger than the labels, as tall as most of the box
constexpr float DigitScale = 1.0f;

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
    const char *digit = static_cast<const Core::LogicInput &>(GetComponent()).IsHigh() ? "1" : "0";
    const float middle = (BoxTop + BoxBottom) / 2.0f;
    const ImVec2 center = LocalToScreen(view, 0.0f, middle);
    // One grid unit long in local space, so its length on screen is the current zoom
    const ImVec2 unit = LocalToScreen(view, 0.0f, middle + 1.0f) - center;
    const float font_size = std::hypot(unit.x, unit.y) * DigitScale;
    if (font_size < MinLabelSize) {
        return;
    }
    ImFont *font = ImGui::GetFont();
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, digit);
    canvas.AddText(font, font_size, center - text_size / 2.0f, color, digit);
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
