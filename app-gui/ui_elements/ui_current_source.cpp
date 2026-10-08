/**
 * @file    ui_current_source.cpp
 * @brief   Schematic symbol of an independent current source.
 */

#include "ui_current_source.h"

#include "components/current_source.h"
#include <memory>

namespace GUI {

namespace {

constexpr float InnerArrowHalfLength = 0.5f;
// The outside arrow sits above the lead on the side the current leaves, like the plus of a voltage source
constexpr float OuterArrowStart = 1.1f;
constexpr float OuterArrowEnd = 1.7f;
constexpr float OuterArrowHeight = 0.4f;
constexpr float ArrowHeadLength = 0.25f;
constexpr float ArrowHeadHalfWidth = 0.2f;

} // namespace

/**
 * @brief   Creates a current source placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UICurrentSource::UICurrentSource(const GridPoint position, const Rotation rotation)
    : UISource(std::make_unique<Core::CurrentSource>(), position, rotation) {
}

// The arrow points from the first terminal to the second, the way a positive current flows through the source.
// ANSI draws it inside the circle; IEC, and any source showing its waveform, draw it outside
void UICurrentSource::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                 const SymbolStyle style) const {
    const auto draw_arrow = [&](const float start, const float end, const float height) {
        canvas.AddLine(LocalToScreen(view, start, height), LocalToScreen(view, end, height), color, LineThickness);
        canvas.AddLine(LocalToScreen(view, end - ArrowHeadLength, height - ArrowHeadHalfWidth),
                       LocalToScreen(view, end, height), color, LineThickness);
        canvas.AddLine(LocalToScreen(view, end - ArrowHeadLength, height + ArrowHeadHalfWidth),
                       LocalToScreen(view, end, height), color, LineThickness);
    };

    DrawCircle(canvas, view, color);
    const bool has_waveform = DrawWaveform(canvas, view, color);
    if (!has_waveform && style == SymbolStyle::ANSI) {
        draw_arrow(-InnerArrowHalfLength, InnerArrowHalfLength, 0.0f);
        return;
    }
    if (!has_waveform) {
        // IEC crosses the circle with a bar perpendicular to the conductor
        canvas.AddLine(LocalToScreen(view, 0, -CircleRadius), LocalToScreen(view, 0, CircleRadius), color,
                       LineThickness);
    }
    draw_arrow(OuterArrowStart, OuterArrowEnd, -OuterArrowHeight);
}

} // namespace GUI
