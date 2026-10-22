/**
 * @file    ui_current_source.cpp
 * @brief   Schematic symbol of an independent current source.
 */

#include "ui_current_source.h"

#include "components/current_source.h"
#include <memory>

namespace GUI {

namespace {

constexpr float INNER_ARROW_HALF_LENGTH = 0.5f;
// The outside arrow sits above the lead on the side the current leaves, like the plus of a voltage source
constexpr float OUTER_ARROW_START = 1.1f;
constexpr float OUTER_ARROW_END = 1.7f;
constexpr float OUTER_ARROW_HEIGHT = 0.4f;
constexpr float ARROW_HEAD_LENGTH = 0.25f;
constexpr float ARROW_HEAD_HALF_WIDTH = 0.2f;

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
        canvas.AddLine(LocalToScreen(view, start, height), LocalToScreen(view, end, height), color, LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, end - ARROW_HEAD_LENGTH, height - ARROW_HEAD_HALF_WIDTH),
                       LocalToScreen(view, end, height), color, LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, end - ARROW_HEAD_LENGTH, height + ARROW_HEAD_HALF_WIDTH),
                       LocalToScreen(view, end, height), color, LINE_THICKNESS);
    };

    DrawCircle(canvas, view, color);
    const bool has_waveform = DrawWaveform(canvas, view, color);
    if (!has_waveform && style == SymbolStyle::ANSI) {
        draw_arrow(-INNER_ARROW_HALF_LENGTH, INNER_ARROW_HALF_LENGTH, 0.0f);
        return;
    }
    if (!has_waveform) {
        // IEC crosses the circle with a bar perpendicular to the conductor
        canvas.AddLine(LocalToScreen(view, 0, -CIRCLE_RADIUS), LocalToScreen(view, 0, CIRCLE_RADIUS), color,
                       LINE_THICKNESS);
    }
    draw_arrow(OUTER_ARROW_START, OUTER_ARROW_END, -OUTER_ARROW_HEIGHT);
}

} // namespace GUI
