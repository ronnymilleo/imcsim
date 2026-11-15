/**
 * @file    live_current.cpp
 * @brief   Draws the live current as arrows flowing along the wires of a schematic.
 */

#include "overlays/live_current.h"

#include "base/helpers.h"
#include "base/node_colors.h"
#include "results/current_flow.h"
#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

/// Distance between flowing-current arrows along the wires, in grid units, so they follow the zoom. The arrows only
/// show direction, so they all have the same size and scroll at one speed; the magnitude stays on the heat color.
constexpr float FLOW_ARROW_SPACING = 0.60f;
constexpr float FLOW_ARROW_LENGTH = 0.20f;
constexpr float FLOW_ARROW_HALF_WIDTH = 0.10f;
/// Speed of the flowing-current arrows, in grid units per second.
constexpr float FLOW_SPEED = 1.5f;

} // namespace

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
void DrawLiveCurrent(SchematicCanvas &canvas, const ViewTransform &view, const Schematic &schematic,
                     const Core::OperatingPoint &operating_point, const HeatScale &scale, const double time) {
    const double largest = scale.LargestCurrent;
    if (largest <= 0.0) {
        return;
    }
    for (const WireCurrent &piece :
         ComputeWireCurrents(schematic.GetElements(), schematic.GetWires(), operating_point.Currents)) {
        const ImVec2 start = ToVec2(piece.Start);
        const ImVec2 end = ToVec2(piece.End);
        const ImVec2 delta = end - start;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        const float level = CurrentLevel(piece.Current, largest);
        if (level < 0.0f || length < FLOW_ARROW_LENGTH) {
            continue;
        }
        // Unit vector along the wire, turned to point the way the current goes
        const ImVec2 along = delta / length;
        const ImVec2 flow = piece.Current >= 0.0 ? along : -along;
        const ImVec2 across = {-flow.y, flow.x};
        // Projection of a point onto the flow direction: an arrow sits wherever it is the scrolling phase plus a
        // multiple of the spacing, so the arrows stay aligned to the grid across the whole schematic. Both ends are
        // projected so the arrows stay on the wire whether the current runs with its start-to-end order or against it
        const float phase = std::fmod(static_cast<float>(time) * FLOW_SPEED, FLOW_ARROW_SPACING);
        const float start_projection = start.x * flow.x + start.y * flow.y;
        const float end_projection = end.x * flow.x + end.y * flow.y;
        const float low = std::min(start_projection, end_projection) + FLOW_ARROW_LENGTH * 0.5f;
        const float high = std::max(start_projection, end_projection) - FLOW_ARROW_LENGTH * 0.5f;
        const ImU32 color = GetHeatColor(std::min(level, 1.0f));
        for (int arrow = static_cast<int>(std::ceil((low - phase) / FLOW_ARROW_SPACING));
             arrow <= static_cast<int>(std::floor((high - phase) / FLOW_ARROW_SPACING)); ++arrow) {
            const float distance = phase + static_cast<float>(arrow) * FLOW_ARROW_SPACING - start_projection;
            const ImVec2 center = start + flow * distance;
            const ImVec2 tip = center + flow * (FLOW_ARROW_LENGTH * 0.5f);
            const ImVec2 back = center - flow * (FLOW_ARROW_LENGTH * 0.5f);
            canvas.AddTriangleFilled(view.ToScreen(tip), view.ToScreen(back + across * FLOW_ARROW_HALF_WIDTH),
                                     view.ToScreen(back - across * FLOW_ARROW_HALF_WIDTH), color);
        }
    }
}

} // namespace GUI
