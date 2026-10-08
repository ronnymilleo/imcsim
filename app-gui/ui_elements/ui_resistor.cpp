/**
 * @file    ui_resistor.cpp
 * @brief   Schematic symbol of a resistor.
 */

#include "ui_resistor.h"

#include "components/resistor.h"
#include <array>
#include <memory>

namespace GUI {

namespace {

constexpr int ZigZagPeakCount = 6;
constexpr float ZigZagStep = 2.0f / ZigZagPeakCount;
constexpr float ZigZagAmplitude = 0.4f;

} // namespace

/**
 * @brief   Creates a resistor placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIResistor::UIResistor(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Resistor>(), position, rotation) {
}

void UIResistor::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                            const SymbolStyle style) const {
    if (style == SymbolStyle::IEC) {
        const ImVec2 rectangle[4] = {LocalToScreen(view, -1, -0.4f), LocalToScreen(view, 1, -0.4f),
                                     LocalToScreen(view, 1, 0.4f), LocalToScreen(view, -1, 0.4f)};
        canvas.AddPolyline(rectangle, 4, color, ImDrawFlags_Closed, LineThickness);
        return;
    }

    // Peaks alternate above and below the axis, evenly spaced between x = -1 and x = 1
    std::array<ImVec2, ZigZagPeakCount + 2> zig_zag;
    zig_zag.front() = LocalToScreen(view, -1, 0);
    for (int peak = 0; peak < ZigZagPeakCount; ++peak) {
        const float x = -1.0f + ZigZagStep * (static_cast<float>(peak) + 0.5f);
        const float y = peak % 2 == 0 ? -ZigZagAmplitude : ZigZagAmplitude;
        zig_zag[peak + 1] = LocalToScreen(view, x, y);
    }
    zig_zag.back() = LocalToScreen(view, 1, 0);
    canvas.AddPolyline(zig_zag.data(), static_cast<int>(zig_zag.size()), color, ImDrawFlags_None, LineThickness);
}

} // namespace GUI
