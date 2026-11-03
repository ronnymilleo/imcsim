/**
 * @file    ui_subcircuit.cpp
 * @brief   Rectangular symbol of a project-local subcircuit instance.
 */

#include "ui_subcircuit.h"

#include "components/subcircuit.h"
#include <array>
#include <memory>

namespace GUI {

namespace {

constexpr float BODY_EDGE = 2.0f;
constexpr int PIN_SPACING = 2;

const Core::SubcircuitInstance &Instance(const Core::Component &component) {
    return static_cast<const Core::SubcircuitInstance &>(component);
}

int SidePinCount(const Core::SubcircuitInstance &instance, const bool right) {
    const int count = static_cast<int>(instance.GetPins().size());
    return right ? count / 2 : (count + 1) / 2;
}

int PinY(const int index, const int side_count) {
    return PIN_SPACING * index - PIN_SPACING * (side_count - 1) / 2;
}

} // namespace

UISubcircuit::UISubcircuit(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::SubcircuitInstance>(), position, rotation) {
}

void UISubcircuit::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    for (const GridPoint pin : GetLocalTerminals()) {
        const float edge = pin.X < 0 ? -BODY_EDGE : BODY_EDGE;
        canvas.AddLine(LocalToScreen(view, static_cast<float>(pin.X), static_cast<float>(pin.Y)),
                       LocalToScreen(view, edge, static_cast<float>(pin.Y)), color, LINE_THICKNESS);
    }
}

void UISubcircuit::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                              const SymbolStyle) const {
    const LocalBounds bounds = GetLocalBounds();
    const std::array box = {LocalToScreen(view, -BODY_EDGE, bounds.Min.y), LocalToScreen(view, BODY_EDGE, bounds.Min.y),
                            LocalToScreen(view, BODY_EDGE, bounds.Max.y),
                            LocalToScreen(view, -BODY_EDGE, bounds.Max.y)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
}

void UISubcircuit::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &instance = Instance(GetComponent());
    const LocalBounds bounds = GetLocalBounds();
    DrawLabel(canvas, view, {0.0f, bounds.Min.y - 0.5f}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    DrawLabel(canvas, view, {0.0f, bounds.Max.y + 0.5f}, {0.0f, 1.0f}, instance.GetDefinitionName(), color);
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    for (std::size_t pin = 0; pin < terminals.size(); ++pin) {
        const bool left = terminals[pin].X < 0;
        DrawLabel(canvas, view, {left ? -BODY_EDGE + 0.2f : BODY_EDGE - 0.2f, static_cast<float>(terminals[pin].Y)},
                  {left ? 1.0f : -1.0f, 0.0f}, instance.GetPins()[pin], color);
    }
}

std::vector<GridPoint> UISubcircuit::GetLocalTerminals() const {
    const auto &instance = Instance(GetComponent());
    std::vector<GridPoint> terminals;
    const int left_count = SidePinCount(instance, false);
    const int right_count = SidePinCount(instance, true);
    for (std::size_t pin = 0; pin < instance.GetPins().size(); ++pin) {
        const bool right = pin >= static_cast<std::size_t>(left_count);
        const int index = static_cast<int>(right ? pin - static_cast<std::size_t>(left_count) : pin);
        terminals.push_back({right ? 3 : -3, PinY(index, right ? right_count : left_count)});
    }
    return terminals;
}

LocalBounds UISubcircuit::GetLocalBounds() const {
    const auto &instance = Instance(GetComponent());
    const int rows = std::max(SidePinCount(instance, false), SidePinCount(instance, true));
    const float half_height = std::max(1.0f, static_cast<float>(PIN_SPACING * rows) / 2.0f);
    return {{-3.0f, -half_height}, {3.0f, half_height}};
}

} // namespace GUI
