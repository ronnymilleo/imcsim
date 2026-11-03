/**
 * @file    ui_subcircuit.cpp
 * @brief   Rectangular symbol of a project-local subcircuit instance.
 */

#include "ui_subcircuit.h"

#include "components/subcircuit.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace GUI {

namespace {

constexpr float BODY_EDGE = 2.0f;
constexpr int PIN_SPACING = 2;
// How far from the middle the terminals are, past the body edge
constexpr int PIN_REACH = 3;

const Core::SubcircuitInstance &Instance(const Core::Component &component) {
    return static_cast<const Core::SubcircuitInstance &>(component);
}

// Inputs go on the left and outputs on the right, each side in the order of the pins
int SidePinCount(const Core::SubcircuitInstance &instance, const bool right) {
    return static_cast<int>(std::ranges::count_if(
        instance.GetPins(), [&](const std::string &pin) { return instance.IsOutputPin(pin) == right; }));
}

int PinY(const int index, const int side_count) {
    return PIN_SPACING * index - PIN_SPACING * (side_count - 1) / 2;
}

} // namespace

/**
 * @brief   Creates the symbol of an instance of no definition yet, with no pins.
 * @param[in] position  Grid point of the middle of the body.
 * @param[in] rotation  Orientation of the symbol.
 */
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

// One terminal per pin, in pin order: inputs down the left side and outputs down the right, each side centered
std::vector<GridPoint> UISubcircuit::GetLocalTerminals() const {
    const auto &instance = Instance(GetComponent());
    std::vector<GridPoint> terminals;
    const int left_count = SidePinCount(instance, false);
    const int right_count = SidePinCount(instance, true);
    int left_index = 0;
    int right_index = 0;
    for (const std::string &pin : instance.GetPins()) {
        if (instance.IsOutputPin(pin)) {
            terminals.push_back({PIN_REACH, PinY(right_index++, right_count)});
        } else {
            terminals.push_back({-PIN_REACH, PinY(left_index++, left_count)});
        }
    }
    return terminals;
}

LocalBounds UISubcircuit::GetLocalBounds() const {
    const auto &instance = Instance(GetComponent());
    const int rows = std::max(SidePinCount(instance, false), SidePinCount(instance, true));
    const float half_height = std::max(1.0f, static_cast<float>(PIN_SPACING * rows) / 2.0f);
    return {{-static_cast<float>(PIN_REACH), -half_height}, {static_cast<float>(PIN_REACH), half_height}};
}

} // namespace GUI
