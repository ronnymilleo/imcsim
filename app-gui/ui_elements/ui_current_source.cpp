/**
 * @file    ui_current_source.cpp
 * @brief   Schematic symbol of an independent current source.
 */

#include "ui_elements/ui_current_source.h"

#include "circuit/components/current_source.h"
#include <memory>

namespace GUI {

namespace {

constexpr float INNER_ARROW_HALF_LENGTH = 0.5f;
/// The outside arrow sits above the lead on the side the current leaves, like the plus of a voltage source.
constexpr float OUTER_ARROW_START = 1.1f;
constexpr float OUTER_ARROW_END = 1.7f;
constexpr float OUTER_ARROW_HEIGHT = 0.4f;
constexpr ArrowHead ARROW_HEAD = {.Length = 0.25f, .HalfWidth = 0.2f};

} // namespace

/**
 * @brief   Creates a current source placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UICurrentSource::UICurrentSource(const GridPoint position, const Rotation rotation)
    : UISource(std::make_unique<Core::CurrentSource>(), position, rotation) {
}

/**
 * @brief   The arrow points from the first terminal to the second, the way a positive current flows through the source.
 * @details ANSI draws it inside the circle; IEC, and any source showing its waveform, draw it outside.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 * @param[in]     style   Drawing standard selecting ANSI inner arrow or IEC crossbar with outer arrow.
 */
void UICurrentSource::DrawSymbol(SchematicCanvas &canvas,
                                 const ViewTransform &view,
                                 const ImU32 color,
                                 const SymbolStyle style) const {
    DrawCircle(canvas, view, color);
    const bool has_waveform = DrawWaveform(canvas, view, color);
    if (!has_waveform && style == SymbolStyle::ANSI) {
        DrawArrow(canvas, view, {-INNER_ARROW_HALF_LENGTH, 0.0f}, {INNER_ARROW_HALF_LENGTH, 0.0f}, ARROW_HEAD, color);
        return;
    }
    if (!has_waveform) {
        // IEC crosses the circle with a bar perpendicular to the conductor
        canvas.AddLine(LocalToScreen(view, 0, -CIRCLE_RADIUS),
                       LocalToScreen(view, 0, CIRCLE_RADIUS),
                       color,
                       LINE_THICKNESS);
    }
    DrawArrow(canvas,
              view,
              {OUTER_ARROW_START, -OUTER_ARROW_HEIGHT},
              {OUTER_ARROW_END, -OUTER_ARROW_HEIGHT},
              ARROW_HEAD,
              color);
}

} // namespace GUI
