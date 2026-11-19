/**
 * @file    live_current.cpp
 * @brief   Draws the live current as arrows flowing along the wires of a schematic.
 */

#include "overlays/live_current.h"

#include "base/helpers.h"
#include "base/node_colors.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace GUI {

namespace {

constexpr float FLOW_ARROW_HALF_WIDTH = 0.10f;
/// Speed of the flowing-current arrows, in grid units per second.
constexpr float FLOW_SPEED = 1.5f;

/**
 * @brief   The unit vector along a piece of wire, turned to point the way its current goes.
 * @return  The direction, or none for a piece too short to hold an arrow.
 */
std::optional<ImVec2> GetFlowDirection(const WireCurrent &piece) {
    const ImVec2 delta = ToVec2(piece.End) - ToVec2(piece.Start);
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length < FLOW_ARROW_LENGTH) {
        return std::nullopt;
    }
    const ImVec2 along = delta / length;
    return piece.Current >= 0.0 ? along : -along;
}

/**
 * @brief   Draws one arrow, a filled triangle centered on a point of the wire and pointing the way the current goes.
 */
void DrawFlowArrow(SchematicCanvas &canvas,
                   const ViewTransform &view,
                   const ImVec2 center,
                   const ImVec2 flow,
                   const ImU32 color) {
    const ImVec2 across = {-flow.y, flow.x};
    const ImVec2 tip = center + flow * (FLOW_ARROW_LENGTH * 0.5f);
    const ImVec2 back = center - flow * (FLOW_ARROW_LENGTH * 0.5f);
    canvas.AddTriangleFilled(view.ToScreen(tip),
                             view.ToScreen(back + across * FLOW_ARROW_HALF_WIDTH),
                             view.ToScreen(back - across * FLOW_ARROW_HALF_WIDTH),
                             color);
}

} // namespace

/**
 * @brief   Where the arrows of a piece of wire sit at a moment of their scrolling.
 * @param[in] piece  The piece of wire.
 * @param[in] flow   Unit vector along the piece, the way its current goes.
 * @param[in] phase  How far the arrows have scrolled, in grid units, from 0 to FLOW_ARROW_SPACING.
 * @return  The centers of the arrows, in world units, from the back of the flow to its front; an arrow sits wherever
 * its projection onto the flow is the phase plus a multiple of the spacing, so the arrows stay aligned to the grid
 *          across the whole schematic, and every arrow stays whole on the piece.
 */
std::vector<ImVec2> ListArrowCenters(const WireCurrent &piece, const ImVec2 flow, const float phase) {
    const ImVec2 start = ToVec2(piece.Start);
    const ImVec2 end = ToVec2(piece.End);
    // Both ends are projected so the arrows stay on the wire whether the current runs with its start-to-end order or
    // against it
    const float start_projection = start.x * flow.x + start.y * flow.y;
    const float end_projection = end.x * flow.x + end.y * flow.y;
    const float low = std::min(start_projection, end_projection) + FLOW_ARROW_LENGTH * 0.5f;
    const float high = std::max(start_projection, end_projection) - FLOW_ARROW_LENGTH * 0.5f;
    std::vector<ImVec2> centers;
    for (int arrow = static_cast<int>(std::ceil((low - phase) / FLOW_ARROW_SPACING));
         arrow <= static_cast<int>(std::floor((high - phase) / FLOW_ARROW_SPACING));
         ++arrow) {
        const float distance = phase + static_cast<float>(arrow) * FLOW_ARROW_SPACING - start_projection;
        centers.push_back(start + flow * distance);
    }
    return centers;
}

/**
 * @brief   Draws the live current as arrows flowing along every piece of wire, in the direction the current goes.
 * @param[in,out] canvas     Where the arrows are drawn: the editor window or an exported image.
 * @param[in] view           Transform from world units to the canvas.
 * @param[in] schematic      Schematic whose parts and wires carry the currents.
 * @param[in] operating_point  The currents to show: an operating point, or a transient read at one instant.
 * @param[in] scale          Heat scale the colors of the arrows follow.
 * @param[in] time           Seconds since an arbitrary start; the arrows scroll with it, so a caller that does not
 *                           animate, such as an export, passes a fixed value.
 * @details Arrows are spaced along each wire and scroll with time; they all look alike, since they only show the
 *          direction, and their color follows the heat scale of the current. Their positions are anchored to the
 *          grid, so arrows line up across collinear pieces instead of jumping where wires join. Pieces whose
 *          current is below the heat scale, down to none at all, are left out: they are noise next to the rest, and
 *          in a transient their direction would flicker.
 */
void DrawLiveCurrent(SchematicCanvas &canvas,
                     const ViewTransform &view,
                     const Schematic &schematic,
                     const Core::OperatingPoint &operating_point,
                     const HeatScale &scale,
                     const double time) {
    const double largest = scale.LargestCurrent;
    if (largest <= 0.0) {
        return;
    }
    // Every arrow scrolls at one speed, so they all share the phase
    const float phase = std::fmod(static_cast<float>(time) * FLOW_SPEED, FLOW_ARROW_SPACING);
    for (const WireCurrent &piece :
         ComputeWireCurrents(schematic.GetElements(), schematic.GetWires(), operating_point.Currents)) {
        const float level = CurrentLevel(piece.Current, largest);
        const std::optional<ImVec2> flow = GetFlowDirection(piece);
        if (level < 0.0f || !flow) {
            continue;
        }
        const ImU32 color = GetHeatColor(std::min(level, 1.0f));
        for (const ImVec2 center : ListArrowCenters(piece, *flow, phase)) {
            DrawFlowArrow(canvas, view, center, *flow, color);
        }
    }
}

} // namespace GUI
