/**
 * @file    ui_inductor.cpp
 * @brief   Schematic symbol of an inductor.
 */

#include "ui_elements/ui_inductor.h"

#include "circuit/components/inductor.h"
#include <memory>

namespace GUI {

namespace {

constexpr int TURN_COUNT = 4;
constexpr float SYMBOL_START = -1.0f;
constexpr float TURN_WIDTH = 2.0f / TURN_COUNT;
constexpr float TURN_RADIUS = TURN_WIDTH / 2.0f;

} // namespace

/**
 * @brief   Creates an inductor placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIInductor::UIInductor(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Inductor>(), position, rotation) {
}

/**
 * @brief   Four turns from one lead to the other, bulging toward -Y.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Line color.
 * @param[in]     style   Drawing standard (unused; coil turns are identical in IEC and ANSI).
 */
void UIInductor::DrawSymbol(SchematicCanvas &canvas,
                            const ViewTransform &view,
                            const ImU32 color,
                            [[maybe_unused]] const SymbolStyle style) const {
    DrawCoilTurns(canvas,
                  view,
                  {.Start = {SYMBOL_START, 0.0f},
                   .Axis = {1.0f, 0.0f},
                   .Bulge = {0.0f, -1.0f},
                   .TurnCount = TURN_COUNT,
                   .TurnRadius = TURN_RADIUS},
                  color);
}

} // namespace GUI
