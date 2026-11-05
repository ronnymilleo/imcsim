/**
 * @file    ui_seven_segment.cpp
 * @brief   Schematic symbol of a 7-segment display that lights the segments whose inputs read 1.
 */

#include "ui_elements/ui_seven_segment.h"

#include "components/seven_segment.h"
#include "drawing/theme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace GUI {

namespace {

// The box in local grid units, with the inputs one unit to the left of it, two units apart
constexpr int TERMINAL_X = -3;
constexpr int FIRST_TERMINAL_Y = -6;
constexpr int TERMINAL_SPACING = 2;
constexpr float BOX_LEFT = -2.0f;
constexpr float BOX_RIGHT = 4.0f;
constexpr float BOX_TOP = -7.0f;
constexpr float BOX_BOTTOM = 7.0f;
/// How far inside the box the names of the inputs go.
constexpr float INPUT_LABEL_INSET = 0.2f;

// The digit, about twice as tall as it is wide, as on a real display
constexpr float DIGIT_LEFT = -0.4f;
constexpr float DIGIT_RIGHT = 3.0f;
constexpr float DIGIT_TOP = -3.6f;
constexpr float DIGIT_MIDDLE = 0.0f;
constexpr float DIGIT_BOTTOM = 3.6f;
constexpr float SEGMENT_THICKNESS = 0.6f;
/// Room left between the ends of two segments that meet at a corner.
constexpr float SEGMENT_GAP = 0.08f;
/// The outline of an unlit segment is thinner than the lines of the symbol, so a lit one stands out.
constexpr float OUTLINE_THICKNESS = 1.0f;
/// A segment whose input is between the logic levels is filled this faintly.
constexpr float UNKNOWN_ALPHA = 0.3f;

/// Where each segment goes, from one end to the other, in the order of Core::SEGMENT_NAMES.
constexpr auto SEGMENTS = std::to_array<std::pair<ImVec2, ImVec2>>({
    {{DIGIT_LEFT, DIGIT_TOP}, {DIGIT_RIGHT, DIGIT_TOP}},
    {{DIGIT_RIGHT, DIGIT_TOP}, {DIGIT_RIGHT, DIGIT_MIDDLE}},
    {{DIGIT_RIGHT, DIGIT_MIDDLE}, {DIGIT_RIGHT, DIGIT_BOTTOM}},
    {{DIGIT_LEFT, DIGIT_BOTTOM}, {DIGIT_RIGHT, DIGIT_BOTTOM}},
    {{DIGIT_LEFT, DIGIT_MIDDLE}, {DIGIT_LEFT, DIGIT_BOTTOM}},
    {{DIGIT_LEFT, DIGIT_TOP}, {DIGIT_LEFT, DIGIT_MIDDLE}},
    {{DIGIT_LEFT, DIGIT_MIDDLE}, {DIGIT_RIGHT, DIGIT_MIDDLE}},
});
static_assert(SEGMENTS.size() == Core::SEGMENT_NAMES.size());

/**
 * @brief   The six corners of a segment, in local grid units: a bar pointed at both ends, so the segments that meet at
 *          a corner fit together.
 */
std::array<ImVec2, 6> GetSegmentCorners(const std::size_t segment) {
    const auto [from, to] = SEGMENTS[segment];
    const float length = std::hypot(to.x - from.x, to.y - from.y);
    const ImVec2 along = {(to.x - from.x) / length, (to.y - from.y) / length};
    const ImVec2 across = {-along.y, along.x};
    const float half = SEGMENT_THICKNESS / 2.0f;
    const ImVec2 start = {from.x + along.x * SEGMENT_GAP, from.y + along.y * SEGMENT_GAP};
    const ImVec2 end = {to.x - along.x * SEGMENT_GAP, to.y - along.y * SEGMENT_GAP};
    return {
        start,
        ImVec2{start.x + (along.x + across.x) * half, start.y + (along.y + across.y) * half},
        ImVec2{end.x + (-along.x + across.x) * half, end.y + (-along.y + across.y) * half},
        end,
        ImVec2{end.x - (along.x + across.x) * half, end.y - (along.y + across.y) * half},
        ImVec2{start.x + (along.x - across.x) * half, start.y + (along.y - across.y) * half},
    };
}

ImU32 WithAlpha(const ImU32 color, const float alpha) {
    ImVec4 components = ImGui::ColorConvertU32ToFloat4(color);
    components.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(components);
}

} // namespace

/**
 * @brief   Creates a 7-segment display placed on the grid, together with its simulation component.
 * @param[in] position  Grid position of its origin, in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UISevenSegment::UISevenSegment(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::SevenSegmentDisplay>(), position, rotation) {
}

/**
 * @brief   Fills the segments that read 1: in the accent of the theme with the Logic Lights effect on, and in the color
 *          of the symbol otherwise, so the digit shows either way; a segment that reads neither is filled faintly.
 * @param[in,out] canvas         Where it is drawn.
 * @param[in]     view           Transform of the current frame.
 * @param[in]     segments       What the input of each segment reads, in the order of Core::SEGMENT_NAMES; none for
 *                               an input the result has no voltage for.
 * @param[in]     color          Color of a lit segment with the effect off.
 * @param[in]     are_lights_on  Whether the Logic Lights effect is on.
 */
void UISevenSegment::DrawReading(SchematicCanvas &canvas, const ViewTransform &view,
                                 const std::span<const std::optional<LogicState>> segments, const ImU32 color,
                                 const bool are_lights_on) const {
    const ImU32 lit = are_lights_on ? ImGui::ColorConvertFloat4ToU32(GetPalette().Accent) : color;
    for (std::size_t segment = 0; segment < std::min(segments.size(), SEGMENTS.size()); ++segment) {
        if (segments[segment] == LogicState::HIGH) {
            FillSegment(canvas, view, segment, lit);
            if (are_lights_on) {
                OutlineSegment(canvas, view, segment, ImGui::ColorConvertFloat4ToU32(GetPalette().AccentHot),
                               OUTLINE_THICKNESS);
            }
        } else if (segments[segment] == LogicState::UNKNOWN) {
            FillSegment(canvas, view, segment, WithAlpha(lit, UNKNOWN_ALPHA));
        }
    }
}

/**
 * @brief   One lead per input, from its terminal to the left edge of the box.
 */
void UISevenSegment::DrawTerminals(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    for (const GridPoint terminal : GetLocalTerminals()) {
        const auto y = static_cast<float>(terminal.Y);
        canvas.AddLine(LocalToScreen(view, static_cast<float>(terminal.X), y), LocalToScreen(view, BOX_LEFT, y), color,
                       LINE_THICKNESS);
    }
}

/**
 * @brief   The box and the outline of every segment; the result fills them through DrawReading().
 */
void UISevenSegment::DrawSymbol(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color,
                                const SymbolStyle /*style*/) const {
    const std::array<ImVec2, 4> box = {LocalToScreen(view, BOX_LEFT, BOX_TOP), LocalToScreen(view, BOX_RIGHT, BOX_TOP),
                                       LocalToScreen(view, BOX_RIGHT, BOX_BOTTOM),
                                       LocalToScreen(view, BOX_LEFT, BOX_BOTTOM)};
    canvas.AddPolyline(box.data(), static_cast<int>(box.size()), color, ImDrawFlags_Closed, LINE_THICKNESS);
    for (std::size_t segment = 0; segment < SEGMENTS.size(); ++segment) {
        OutlineSegment(canvas, view, segment, color, OUTLINE_THICKNESS);
    }
}

/**
 * @brief   The name of each input inside the box, next to its lead; the display has no name or value to show.
 */
void UISevenSegment::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::vector<GridPoint> terminals = GetLocalTerminals();
    for (std::size_t input = 0; input < terminals.size(); ++input) {
        DrawLabel(canvas, view, {BOX_LEFT + INPUT_LABEL_INSET, static_cast<float>(terminals[input].Y)}, {1.0f, 0.0f},
                  Core::SEGMENT_NAMES[input], color);
    }
}

/**
 * @brief   One input per segment down the left of the box, a at the top and g at the bottom.
 */
std::vector<GridPoint> UISevenSegment::GetLocalTerminals() const {
    std::vector<GridPoint> terminals;
    for (std::size_t input = 0; input < Core::SEGMENT_NAMES.size(); ++input) {
        terminals.push_back({TERMINAL_X, FIRST_TERMINAL_Y + TERMINAL_SPACING * static_cast<int>(input)});
    }
    return terminals;
}

/**
 * @brief   Every lead goes right, into the box, even the inputs at its top and bottom.
 */
GridPoint UISevenSegment::GetLocalTerminalInward(std::size_t /*terminal*/) const {
    return {1, 0};
}

LocalBounds UISevenSegment::GetLocalBounds() const {
    return {{static_cast<float>(TERMINAL_X), BOX_TOP}, {BOX_RIGHT, BOX_BOTTOM}};
}

void UISevenSegment::FillSegment(SchematicCanvas &canvas, const ViewTransform &view, const std::size_t segment,
                                 const ImU32 color) const {
    const std::array<ImVec2, 6> screen = GetSegmentOnScreen(view, segment);
    // A fan from the first corner covers the convex outline
    for (std::size_t corner = 1; corner + 1 < screen.size(); ++corner) {
        canvas.AddTriangleFilled(screen[0], screen[corner], screen[corner + 1], color);
    }
}

void UISevenSegment::OutlineSegment(SchematicCanvas &canvas, const ViewTransform &view, const std::size_t segment,
                                    const ImU32 color, const float thickness) const {
    const std::array<ImVec2, 6> screen = GetSegmentOnScreen(view, segment);
    canvas.AddPolyline(screen.data(), static_cast<int>(screen.size()), color, ImDrawFlags_Closed, thickness);
}

std::array<ImVec2, 6> UISevenSegment::GetSegmentOnScreen(const ViewTransform &view, const std::size_t segment) const {
    const std::array<ImVec2, 6> corners = GetSegmentCorners(segment);
    std::array<ImVec2, 6> screen{};
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        screen[corner] = LocalToScreen(view, corners[corner].x, corners[corner].y);
    }
    return screen;
}

} // namespace GUI
