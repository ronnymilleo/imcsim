/**
 * @file    ui_igbt.cpp
 * @brief   Schematic symbol of an N-channel IGBT.
 */

#include "ui_elements/ui_igbt.h"

#include "circuit/components/igbt.h"
#include <memory>

namespace GUI {

namespace {

constexpr float GATE_X = -0.6f;
constexpr float BAR_X = -0.3f;
constexpr float PLATE_HALF_HEIGHT = 0.6f;
constexpr float BAR_HALF_HEIGHT = 0.7f;
/// Collector and emitter leave the bar this far from its middle.
constexpr float JUNCTION_Y = 0.35f;
/// Where along the emitter the arrow tip sits, as a share of its length.
constexpr float ARROW_POSITION = 0.75f;

} // namespace

/**
 * @brief   Creates an IGBT placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIIGBT::UIIGBT(const GridPoint position, const Rotation rotation)
    : UITransistor(std::make_unique<Core::IGBT>(), position, rotation) {
}

void UIIGBT::DrawSymbol(SchematicCanvas &canvas,
                        const ViewTransform &view,
                        const ImU32 color,
                        SymbolStyle /*style*/) const {
    // The insulated gate: a plate apart from the bar, as on a MOSFET
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, GATE_X, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, GATE_X, -PLATE_HALF_HEIGHT),
                   LocalToScreen(view, GATE_X, PLATE_HALF_HEIGHT),
                   color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, -BAR_HALF_HEIGHT),
                   LocalToScreen(view, BAR_X, BAR_HALF_HEIGHT),
                   color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, -JUNCTION_Y), LocalToScreen(view, 1, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, JUNCTION_Y), LocalToScreen(view, 1, 1), color, LINE_THICKNESS);

    const ImVec2 emitter = {1.0f - BAR_X, 1.0f - JUNCTION_Y};
    const ImVec2 tip = {BAR_X + emitter.x * ARROW_POSITION, JUNCTION_Y + emitter.y * ARROW_POSITION};
    DrawFilledArrowHead(canvas, view, tip, emitter, ARROW_HEAD, color);
}

const char *UIIGBT::GetModelName() const {
    return static_cast<const Core::IGBT &>(GetComponent()).GetModelName();
}

} // namespace GUI
