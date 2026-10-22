/**
 * @file    ui_bjt.cpp
 * @brief   Schematic symbol of an NPN or PNP bipolar transistor.
 */

#include "ui_bjt.h"

#include "components/bjt.h"
#include <memory>

namespace GUI {

namespace {

constexpr float BAR_X = -0.4f;
constexpr float BAR_HALF_HEIGHT = 0.6f;
// Where the collector and emitter leave the bar
constexpr float JUNCTION_Y = 0.3f;
// How far along the emitter, from the bar, the arrow tip sits
constexpr float NPN_ARROW_POSITION = 0.8f;
constexpr float PNP_ARROW_POSITION = 0.3f;

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
    canvas.AddLine(LocalToScreen(view, -1, 0), LocalToScreen(view, BAR_X, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, -BAR_HALF_HEIGHT), LocalToScreen(view, BAR_X, BAR_HALF_HEIGHT), color,
                   LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, -JUNCTION_Y), LocalToScreen(view, 1, -1), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, BAR_X, JUNCTION_Y), LocalToScreen(view, 1, 1), color, LINE_THICKNESS);

    const ImVec2 emitter_start = {BAR_X, JUNCTION_Y};
    const ImVec2 emitter = {1.0f - BAR_X, 1.0f - JUNCTION_Y};
    if (GetComponent().GetType() == Core::ComponentType::NPN) {
        const ImVec2 tip = {emitter_start.x + emitter.x * NPN_ARROW_POSITION,
                            emitter_start.y + emitter.y * NPN_ARROW_POSITION};
        DrawArrowHead(canvas, view, tip, emitter, color);
    } else {
        const ImVec2 tip = {emitter_start.x + emitter.x * PNP_ARROW_POSITION,
                            emitter_start.y + emitter.y * PNP_ARROW_POSITION};
        DrawArrowHead(canvas, view, tip, {-emitter.x, -emitter.y}, color);
    }
}

const char *UIBJT::GetModelName() const {
    return static_cast<const Core::BJT &>(GetComponent()).GetModelName();
}

} // namespace GUI
