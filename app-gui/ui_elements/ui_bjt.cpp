/**
 * @file    ui_bjt.cpp
 * @brief   Schematic symbol of an NPN or PNP bipolar transistor.
 */

#include "ui_bjt.h"

#include "components/bjt.h"
#include <memory>

namespace GUI {

namespace {

constexpr float BarX = -0.4f;
constexpr float BarHalfHeight = 0.6f;
// Where the collector and emitter leave the bar
constexpr float JunctionY = 0.3f;
// How far along the emitter, from the bar, the arrow tip sits
constexpr float NPNArrowPosition = 0.8f;
constexpr float PNPArrowPosition = 0.3f;

} // namespace

/**
 * @brief   Creates a bipolar transistor placed on the grid, together with its simulation component.
 * @param[in] type      Core::ComponentType::NPN or PNP.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIBJT::UIBJT(const Core::ComponentType type, const GridPoint position, const Rotation rotation)
    : UITransistor(std::make_unique<Core::BJT>(type), position, rotation) {
}

void UIBJT::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                       SymbolStyle /*style*/) const {
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, BarX, 0), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, BarX, -BarHalfHeight), LocalToScreen(view, BarX, BarHalfHeight), color,
                   LineThickness);
    canvas.AddLine(LocalToScreen(view, BarX, -JunctionY), LocalToScreen(view, 1, -1), color, LineThickness);
    canvas.AddLine(LocalToScreen(view, BarX, JunctionY), LocalToScreen(view, 1, 1), color, LineThickness);

    const ImVec2 emitter_start = {BarX, JunctionY};
    const ImVec2 emitter = {1.0f - BarX, 1.0f - JunctionY};
    if (GetComponent().GetType() == Core::ComponentType::NPN) {
        const ImVec2 tip = {emitter_start.x + emitter.x * NPNArrowPosition,
                            emitter_start.y + emitter.y * NPNArrowPosition};
        DrawArrowHead(canvas, view, tip, emitter, color);
    } else {
        const ImVec2 tip = {emitter_start.x + emitter.x * PNPArrowPosition,
                            emitter_start.y + emitter.y * PNPArrowPosition};
        DrawArrowHead(canvas, view, tip, {-emitter.x, -emitter.y}, color);
    }
}

const char *UIBJT::GetModelName() const {
    return static_cast<const Core::BJT &>(GetComponent()).GetModelName();
}

} // namespace GUI
