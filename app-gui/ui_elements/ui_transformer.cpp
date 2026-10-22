/**
 * @file    ui_transformer.cpp
 * @brief   Schematic symbol of a transformer.
 */

#include "ui_transformer.h"

#include "components/transformer.h"
#include "spice_value.h"
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>

namespace GUI {

namespace {

// Each coil runs from y = -1 to y = 1 in turns that bulge toward the core
constexpr float COIL_X = 0.9f;
constexpr int TURN_COUNT = 3;
constexpr int SEGMENTS_PER_TURN = 8;
constexpr float TURN_HEIGHT = 2.0f / TURN_COUNT;
constexpr float TURN_RADIUS = TURN_HEIGHT / 2.0f;
constexpr float CORE_X = 0.2f;
constexpr float DOT_RADIUS = 0.1f;
constexpr ImVec2 PRIMARY_DOT = {-1.4f, -1.35f};
constexpr ImVec2 SECONDARY_DOT = {1.4f, -1.35f};
constexpr float NAME_OFFSET_Y = 1.4f;

} // namespace

/**
 * @brief   Creates a transformer placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UITransformer::UITransformer(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Transformer>(), position, rotation) {
}

// The leads run to the ends of the coils, one unit apart from the core on each side
void UITransformer::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    for (const float y : {-1.0f, 1.0f}) {
        canvas.AddLine(LocalToScreen(view, -2, y), LocalToScreen(view, -COIL_X, y), color, LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, 2, y), LocalToScreen(view, COIL_X, y), color, LINE_THICKNESS);
    }
}

void UITransformer::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                               const SymbolStyle /*style*/) const {
    DrawCoil(canvas, view, color, -COIL_X, 1.0f);
    DrawCoil(canvas, view, color, COIL_X, -1.0f);
    for (const float x : {-CORE_X, CORE_X}) {
        canvas.AddLine(LocalToScreen(view, x, -1), LocalToScreen(view, x, 1), color, LINE_THICKNESS);
    }
    const ImVec2 origin = LocalToScreen(view, 0, 0);
    const ImVec2 unit = LocalToScreen(view, 1, 0) - origin;
    const float radius = std::hypot(unit.x, unit.y) * DOT_RADIUS;
    canvas.AddCircleFilled(LocalToScreen(view, PRIMARY_DOT.x, PRIMARY_DOT.y), radius, color);
    canvas.AddCircleFilled(LocalToScreen(view, SECONDARY_DOT.x, SECONDARY_DOT.y), radius, color);
}

// The primary inductance and the turns ratio, such as "100m 2:1"
void UITransformer::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const auto &transformer = static_cast<const Core::Transformer &>(GetComponent());
    DrawLabel(canvas, view, {0.0f, -NAME_OFFSET_Y}, {0.0f, -1.0f}, transformer.GetName(), color);
    DrawLabel(
        canvas, view, {0.0f, NAME_OFFSET_Y}, {0.0f, 1.0f},
        std::format("{} {:g}:1", Core::FormatValue(transformer.GetValue()), transformer.GetParameters().TurnsRatio),
        color);
}

/**
 * @brief   Returns the terminals in component order, in local grid units before rotation.
 * @return  Primary + at (-2, -1) and - at (-2, 1), then secondary + at (2, -1) and - at (2, 1).
 */
std::vector<GridPoint> UITransformer::GetLocalTerminals() const {
    return {{-2, -1}, {-2, 1}, {2, -1}, {2, 1}};
}

/**
 * @brief   Returns the pick area of the transformer, in local grid units before rotation.
 * @return  A box from the primary leads to the secondary ones, around the coils.
 */
LocalBounds UITransformer::GetLocalBounds() const {
    return {{-2.0f, -1.5f}, {2.0f, 1.5f}};
}

// Each turn is its own polyline, sampled in local coordinates so the arcs follow the rotation, like an inductor's;
// bulge is +1 toward +X and -1 toward -X
void UITransformer::DrawCoil(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color, const float x,
                             const float bulge) const {
    std::array<ImVec2, SEGMENTS_PER_TURN + 1> points;
    for (int turn = 0; turn < TURN_COUNT; ++turn) {
        const float center_y = -1.0f + TURN_RADIUS + TURN_HEIGHT * static_cast<float>(turn);
        for (int segment = 0; segment <= SEGMENTS_PER_TURN; ++segment) {
            // From the top of the turn to its bottom through the side the coil bulges to
            const float angle = std::numbers::pi_v<float> *
                                (-0.5f + static_cast<float>(segment) / static_cast<float>(SEGMENTS_PER_TURN));
            points[segment] = LocalToScreen(view, x + bulge * TURN_RADIUS * std::cos(angle),
                                            center_y + TURN_RADIUS * std::sin(angle));
        }
        canvas.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, LINE_THICKNESS);
    }
}

} // namespace GUI
