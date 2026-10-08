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

constexpr int TurnCount = 4;
constexpr int SegmentsPerTurn = 8;
constexpr float SymbolStart = -1.0f;
constexpr float TurnWidth = 2.0f / TurnCount;
constexpr float TurnRadius = TurnWidth / 2.0f;

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
    std::array<ImVec2, SegmentsPerTurn + 1> points;
    for (int turn = 0; turn < TurnCount; ++turn) {
        const float center_x = SymbolStart + TurnRadius + TurnWidth * static_cast<float>(turn);
        for (int segment = 0; segment <= SegmentsPerTurn; ++segment) {
            // Sweep from the left end of the turn (angle pi) to the right end (angle 0), bulging toward -Y
            const float angle =
                std::numbers::pi_v<float> * (1.0f - static_cast<float>(segment) / static_cast<float>(SegmentsPerTurn));
            const float x = center_x + TurnRadius * std::cos(angle);
            const float y = -TurnRadius * std::sin(angle);
            points[segment] = LocalToScreen(view, x, y);
        }
        canvas.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, LineThickness);
    }
}

} // namespace GUI
