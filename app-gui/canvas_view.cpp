/**
 * @file    canvas_view.cpp
 * @brief   Pan and zoom of the schematic canvas, and the framing that shows the whole schematic.
 */

#include "canvas_view.h"

#include "schematic_drawing.h"
#include <algorithm>
#include <utility>

namespace GUI {

namespace {

constexpr float MinZoom = 4.0f;
constexpr float MaxZoom = 200.0f;
constexpr float ZoomStep = 1.1f;
// Fit may zoom in this far on a small circuit; opening a file stops at the default zoom
constexpr float FitMaxZoom = 50.0f;

} // namespace

/**
 * @brief   Asks the next frame to show the whole schematic, zooming in up to FitMaxZoom on a small one, as the Fit
 *          button and Home do.
 */
void CanvasView::RequestFit() {
    m_FrameMaxZoom = FitMaxZoom;
}

/**
 * @brief   Asks the next frame to show the whole schematic without zooming in past the default zoom, as when a file
 *          or an example opens.
 */
void CanvasView::RequestDefaultFrame() {
    m_FrameMaxZoom = DefaultZoom;
}

/**
 * @brief   Shows the whole schematic centered, labels included, when a framing was requested; a small circuit is not
 *          blown up past the zoom of the request.
 * @param[in] schematic         Schematic to frame.
 * @param[in] canvas_size       Size of the canvas this frame, in pixels.
 * @param[in] style             Symbol style, which changes the size of the parts.
 * @param[in] terminal_numbers  Whether terminal numbers are drawn, which they take room for.
 */
void CanvasView::FrameIfRequested(Schematic &schematic, const ImVec2 canvas_size, const SymbolStyle style,
                                  const bool terminal_numbers) {
    const std::optional<float> max_zoom = std::exchange(m_FrameMaxZoom, std::nullopt);
    if (!max_zoom) {
        return;
    }
    const std::optional<SchematicBounds> bounds = MeasureSchematic(schematic, style, terminal_numbers);
    const ViewFrame frame = bounds ? FrameBounds(bounds->Min, bounds->Max, canvas_size, MinZoom, *max_zoom)
                                   : FramePoints({}, canvas_size, MinZoom, *max_zoom);
    m_Pan = frame.Pan;
    m_Zoom = frame.Zoom;
}

/**
 * @brief   Pans while the middle button drags the canvas, and zooms with the wheel around the point under the cursor.
 * @param[in] origin   Top left corner of the canvas on screen.
 * @param[in] hovered  Whether the mouse is over the canvas.
 * @param[in] active   Whether the canvas holds a mouse button.
 */
void CanvasView::HandlePanAndZoom(const ImVec2 origin, const bool hovered, const bool active) {
    const ImGuiIO &io = ImGui::GetIO();
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_Pan += io.MouseDelta;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        // Keep the world point under the cursor fixed while zooming
        const ImVec2 before = ViewTransform(origin, m_Pan, m_Zoom).ToWorld(io.MousePos);
        m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? ZoomStep : 1 / ZoomStep), MinZoom, MaxZoom);
        const ImVec2 after = ViewTransform(origin, m_Pan, m_Zoom).ToWorld(io.MousePos);
        m_Pan += (after - before) * m_Zoom;
    }
}

/**
 * @brief   Converts between grid units and screen pixels for a canvas.
 * @param[in] origin  Top left corner of the canvas on screen.
 * @return  The transform of the current pan and zoom.
 */
ViewTransform CanvasView::GetTransform(const ImVec2 origin) const {
    return {origin, m_Pan, m_Zoom};
}

/**
 * @brief   Tells how large one grid unit is on screen.
 * @return  The zoom, in pixels per grid unit.
 */
float CanvasView::GetZoom() const {
    return m_Zoom;
}

} // namespace GUI
