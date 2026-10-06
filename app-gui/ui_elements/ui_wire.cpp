/**
 * @file    ui_wire.cpp
 * @brief   Straight wire segment between two grid points of the schematic.
 */

#include "ui_wire.h"

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

} // namespace GUI
