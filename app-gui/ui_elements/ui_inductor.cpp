/**
 * @file    ui_inductor.cpp
 * @brief   Schematic symbol of an inductor.
 */

#include "ui_inductor.h"

#include "components/inductor.h"
#include <array>
#include <cmath>
#include <memory>
#include <numbers>

namespace GUI {

namespace {

constexpr int TURN_COUNT = 4;
constexpr int SEGMENTS_PER_TURN = 8;
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

// Each turn is sampled in local coordinates instead of using PathArcTo, so the arcs follow the element rotation.
// Turns are separate polylines: joining them would put a near 180 degree corner where two turns meet, and ImGui
// extends such miter joins into visible spikes
void UIInductor::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                            SymbolStyle /*style*/) const {
    std::array<ImVec2, SEGMENTS_PER_TURN + 1> points;
    for (int turn = 0; turn < TURN_COUNT; ++turn) {
        const float center_x = SYMBOL_START + TURN_RADIUS + TURN_WIDTH * static_cast<float>(turn);
        for (int segment = 0; segment <= SEGMENTS_PER_TURN; ++segment) {
            // Sweep from the left end of the turn (angle pi) to the right end (angle 0), bulging toward -Y
            const float angle = std::numbers::pi_v<float> *
                                (1.0f - static_cast<float>(segment) / static_cast<float>(SEGMENTS_PER_TURN));
            const float x = center_x + TURN_RADIUS * std::cos(angle);
            const float y = -TURN_RADIUS * std::sin(angle);
            points[segment] = LocalToScreen(view, x, y);
        }
        canvas.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, LINE_THICKNESS);
    }
}

} // namespace GUI
