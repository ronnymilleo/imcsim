/**
 * @file    ui_fuse.cpp
 * @brief   Schematic symbol of a fuse.
 */

#include "ui_elements/ui_fuse.h"

#include "circuit/components/fuse.h"
#include "drawing/theme.h"
#include "spice/spice_value.h"
#include <cmath>
#include <memory>
#include <numbers>

namespace GUI {

namespace {

constexpr float BODY_HALF_LENGTH = 1.0f;
constexpr float BODY_HALF_HEIGHT = 0.4f;
/// A blown fuse keeps the line this far from the middle on each side, and the gap between the two ends is marked.
constexpr float GAP_HALF_LENGTH = 0.3f;
constexpr float BREAK_MARK_HALF_HEIGHT = 0.25f;
constexpr float RATING_OFFSET_Y = 0.9f;
constexpr int FLASH_RAY_COUNT = 8;
constexpr float FLASH_INNER_RADIUS = 0.5f;
constexpr float FLASH_OUTER_RADIUS = 1.4f;
constexpr ImU32 OPAQUE_ALPHA = 255;

ImU32 GetBlownColor(const ImU32 alpha) {
    ImVec4 color = GetErrorTextColor();
    color.w = static_cast<float>(alpha) / 255.0f;
    return ImGui::ColorConvertFloat4ToU32(color);
}

} // namespace

/**
 * @brief   Creates a fuse placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIFuse::UIFuse(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::Fuse>(), position, rotation) {
}

/**
 * @brief   Sets what the result on display shows of the fuse.
 * @param[in] is_blown     Whether the fuse is open at the instant on display.
 * @param[in] flash_level  Strength of the flash that marks the blow, from 0 to 1.
 */
void UIFuse::SetDisplayState(const bool is_blown, const float flash_level) {
    m_IsBlown = is_blown;
    m_FlashLevel = flash_level;
}

void UIFuse::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                        const SymbolStyle /*style*/) const {
    const ImVec2 body[4] = {LocalToScreen(view, -BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                            LocalToScreen(view, BODY_HALF_LENGTH, -BODY_HALF_HEIGHT),
                            LocalToScreen(view, BODY_HALF_LENGTH, BODY_HALF_HEIGHT),
                            LocalToScreen(view, -BODY_HALF_LENGTH, BODY_HALF_HEIGHT)};
    canvas.AddPolyline(body, 4, color, ImDrawFlags_Closed, LINE_THICKNESS);
    if (!m_IsBlown) {
        canvas.AddLine(LocalToScreen(view, -BODY_HALF_LENGTH, 0), LocalToScreen(view, BODY_HALF_LENGTH, 0), color,
                       LINE_THICKNESS);
        return;
    }
    const ImU32 blown_color = GetBlownColor(OPAQUE_ALPHA);
    for (const float side : {-1.0f, 1.0f}) {
        canvas.AddLine(LocalToScreen(view, side * BODY_HALF_LENGTH, 0), LocalToScreen(view, side * GAP_HALF_LENGTH, 0),
                       color, LINE_THICKNESS);
        canvas.AddLine(LocalToScreen(view, side * GAP_HALF_LENGTH, -BREAK_MARK_HALF_HEIGHT),
                       LocalToScreen(view, side * GAP_HALF_LENGTH, BREAK_MARK_HALF_HEIGHT), blown_color,
                       LINE_THICKNESS);
    }
    DrawFlash(canvas, view);
}

/**
 * @brief   The name above and the rated current under the part, which says "blown" in its place once it is open.
 */
void UIFuse::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawLabel(canvas, view, {0.0f, -RATING_OFFSET_Y}, {0.0f, -1.0f}, GetComponent().GetName(), color);
    if (m_IsBlown) {
        DrawLabel(canvas, view, {0.0f, RATING_OFFSET_Y}, {0.0f, 1.0f}, "blown", GetBlownColor(OPAQUE_ALPHA));
        return;
    }
    const double rated_current = static_cast<const Core::Fuse &>(GetComponent()).GetParameters().RatedCurrent;
    DrawLabel(canvas, view, {0.0f, RATING_OFFSET_Y}, {0.0f, 1.0f}, Core::FormatValue(rated_current) + "A", color);
}

/**
 * @brief   Rays that burst from the middle of the body and fade with the flash level.
 */
void UIFuse::DrawFlash(SchematicCanvas &canvas, const ViewTransform &view) const {
    if (m_FlashLevel <= 0.0f) {
        return;
    }
    const ImU32 color = GetBlownColor(static_cast<ImU32>(m_FlashLevel * static_cast<float>(OPAQUE_ALPHA)));
    for (int ray = 0; ray < FLASH_RAY_COUNT; ++ray) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(ray) / FLASH_RAY_COUNT;
        const float reach = FLASH_INNER_RADIUS + (FLASH_OUTER_RADIUS - FLASH_INNER_RADIUS) * (1.0f - m_FlashLevel);
        canvas.AddLine(LocalToScreen(view, FLASH_INNER_RADIUS * std::cos(angle), FLASH_INNER_RADIUS * std::sin(angle)),
                       LocalToScreen(view, reach * std::cos(angle), reach * std::sin(angle)), color, LINE_THICKNESS);
    }
}

} // namespace GUI
