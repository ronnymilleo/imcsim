/**
 * @file    ui_wire.cpp
 * @brief   Straight wire segment between two grid points of the schematic.
 */

#include "ui_wire.h"

#include <algorithm>

namespace GUI {

/**
 * @brief   Creates a wire segment.
 * @param[in] start  First end on the grid.
 * @param[in] end    Second end on the grid.
 */
UIWire::UIWire(const GridPoint start, const GridPoint end) : m_Start(start), m_End(end) {
}

/**
 * @brief   Draws the wire segment.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color, so the same wire can be drawn as a preview.
 */
void UIWire::Draw(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const ImVec2 start = view.ToScreen(ToVec2(m_Start));
    const ImVec2 end = view.ToScreen(ToVec2(m_End));
    draw_list->AddLine(start, end, color, LineThickness);

    // Lines have flat ends, so two segments meeting at a corner leave a notch; a square on each end fills it
    const ImVec2 half_square = {LineThickness / 2.0f, LineThickness / 2.0f};
    draw_list->AddRectFilled(start - half_square, start + half_square, color);
    draw_list->AddRectFilled(end - half_square, end + half_square, color);
}

/**
 * @brief   Returns the first end of the wire.
 * @return  Grid point where the segment starts.
 */
GridPoint UIWire::GetStart() const {
    return m_Start;
}

/**
 * @brief   Returns the second end of the wire.
 * @return  Grid point where the segment ends.
 */
GridPoint UIWire::GetEnd() const {
    return m_End;
}

/**
 * @brief   Checks whether a grid point lies strictly between the two ends of the wire.
 * @param[in] point  Grid point to test.
 * @return  True when the point is on the wire but is not one of its ends.
 * @note    Wires are always horizontal or vertical, so only the axis the wire runs along is checked.
 */
bool UIWire::PassesThrough(const GridPoint point) const {
    if (m_Start.X == m_End.X && point.X == m_Start.X) {
        return point.Y > std::min(m_Start.Y, m_End.Y) && point.Y < std::max(m_Start.Y, m_End.Y);
    }
    if (m_Start.Y == m_End.Y && point.Y == m_Start.Y) {
        return point.X > std::min(m_Start.X, m_End.X) && point.X < std::max(m_Start.X, m_End.X);
    }
    return false;
}

/**
 * @brief   Checks whether a world position is close enough to the wire to pick it with the mouse.
 * @param[in] world_pos  Position in world units, usually the cursor.
 * @param[in] tolerance  Maximum distance in world units.
 * @return  True when the position is within the tolerance of the segment.
 */
bool UIWire::IsNear(const ImVec2 world_pos, const float tolerance) const {
    // The segment is axis-aligned, so its bounding box grown by the tolerance is the pick area
    const float min_x = static_cast<float>(std::min(m_Start.X, m_End.X)) - tolerance;
    const float max_x = static_cast<float>(std::max(m_Start.X, m_End.X)) + tolerance;
    const float min_y = static_cast<float>(std::min(m_Start.Y, m_End.Y)) - tolerance;
    const float max_y = static_cast<float>(std::max(m_Start.Y, m_End.Y)) + tolerance;
    return world_pos.x >= min_x && world_pos.x <= max_x && world_pos.y >= min_y && world_pos.y <= max_y;
}

} // namespace GUI
