/**
 * @file    ui_resistor.cpp
 * @brief   Schematic symbol of a resistor.
 */

#include "ui_resistor.h"

#include "components/resistor.h"
#include <array>
#include <memory>
#include <utility>

namespace GUI {

namespace {

constexpr int ZIG_ZAG_PEAK_COUNT = 6;
constexpr float ZIG_ZAG_STEP = 2.0f / ZIG_ZAG_PEAK_COUNT;
constexpr float ZIG_ZAG_AMPLITUDE = 0.4f;

} // namespace

/**
 * @brief   Creates a resistor placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIResistor::UIResistor(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Resistor>(), position, rotation) {
}

UIResistor::UIResistor(std::unique_ptr<Core::Component> component, const GridPoint position, const Rotation rotation)
    : UIElement(std::move(component), position, rotation) {
}

void UIResistor::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                            const SymbolStyle style) const {
    if (style == SymbolStyle::IEC) {
        const ImVec2 rectangle[4] = {LocalToScreen(view, -1, -0.4f), LocalToScreen(view, 1, -0.4f),
                                     LocalToScreen(view, 1, 0.4f), LocalToScreen(view, -1, 0.4f)};
        canvas.AddPolyline(rectangle, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
        return;
    }

    // Peaks alternate above and below the axis, evenly spaced between x = -1 and x = 1
    std::array<ImVec2, ZIG_ZAG_PEAK_COUNT + 2> zig_zag;
    zig_zag.front() = LocalToScreen(view, -1, 0);
    for (int peak = 0; peak < ZIG_ZAG_PEAK_COUNT; ++peak) {
        const float x = -1.0f + ZIG_ZAG_STEP * (static_cast<float>(peak) + 0.5f);
        const float y = peak % 2 == 0 ? -ZIG_ZAG_AMPLITUDE : ZIG_ZAG_AMPLITUDE;
        zig_zag[peak + 1] = LocalToScreen(view, x, y);
    }
    zig_zag.back() = LocalToScreen(view, 1, 0);
    canvas.AddPolyline(zig_zag.data(), static_cast<int>(zig_zag.size()), color, ImDrawFlags_None, LINE_THICKNESS);
}

} // namespace GUI
