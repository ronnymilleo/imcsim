/**
 * @file    editor_canvas_drawing.cpp
 * @brief   What the editor draws on its canvas besides the schematic: the grid, the selection box and the shown
 * instant.
 */

#include "windows/editor_canvas_drawing.h"

#include "base/theme.h"
#include "schematic/canvas_view.h"
#include "spice/spice_value.h"
#include <cmath>
#include <format>
#include <string>

namespace GUI {

namespace {

/// Space between the edge of the canvas and the instant it shows, in font sizes.
constexpr float INSTANT_MARGIN = 0.5f;
constexpr ImU32 SELECTION_BOX_FILL_ALPHA = 40;

ImU32 WithAlpha(const ImU32 color, const ImU32 alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

} // namespace

/**
 * @brief   Draws the dots of the grid that the canvas shows, thinned out when zoomed far out.
 * @param[in] draw_list  Where to draw.
 * @param[in] view       Pan and zoom of the canvas.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 * @param[in] size       Size of the canvas, in screen pixels.
 * @param[in] zoom       Pixels per grid unit.
 */
void DrawGrid(ImDrawList *draw_list, const ViewTransform &view, const ImVec2 origin, const ImVec2 size,
              const float zoom) {
    const float step_size = static_cast<float>(GetGridStep(zoom));

    const ImVec2 world_min = view.ToWorld(origin);
    const ImVec2 world_max = view.ToWorld(origin + size);
    const ImVec2 dot_half_size = {1.0f, 1.0f};
    const int first_column = static_cast<int>(std::floor(world_min.x / step_size));
    const int last_column = static_cast<int>(std::floor(world_max.x / step_size));
    const int first_row = static_cast<int>(std::floor(world_min.y / step_size));
    const int last_row = static_cast<int>(std::floor(world_max.y / step_size));
    for (int column = first_column; column <= last_column; ++column) {
        for (int row = first_row; row <= last_row; ++row) {
            const ImVec2 dot =
                view.ToScreen({static_cast<float>(column) * step_size, static_cast<float>(row) * step_size});
            draw_list->AddRectFilled(dot - dot_half_size, dot + dot_half_size, GetPalette().GridDot);
        }
    }
}

/**
 * @brief   The box and the items it takes in, drawn over the schematic while the editor is dragging one.
 * @param[in] draw_list  Where to draw.
 * @param[in] view       Pan and zoom of the canvas.
 * @param[in] mode       The mode of the editor, which holds the box.
 */
void DrawSelectionBox(ImDrawList *draw_list, const ViewTransform &view, const EditorMode &mode) {
    const auto *selecting = std::get_if<SelectingMode>(&mode);
    if (selecting == nullptr || !selecting->Box) {
        return;
    }
    const ImVec2 start = view.ToScreen(selecting->Box->Start);
    const ImVec2 end = ImGui::GetIO().MousePos;
    const ImU32 color = GetPalette().Selected;
    draw_list->AddRectFilled(start, end, WithAlpha(color, SELECTION_BOX_FILL_ALPHA));
    draw_list->AddRect(start, end, color, 0.0f, 0, 1.0f);
}

/**
 * @brief   Writes the instant of the transient the schematic shows at the top left of the canvas, so it reads while the
 *          playback, under the plot of the Output window, moves it.
 * @param[in] draw_list  Where to draw.
 * @param[in] origin     Top left corner of the canvas, in screen pixels.
 * @param[in] time       The instant, in seconds, or none when the schematic shows no transient.
 */
void DrawShownInstant(ImDrawList *draw_list, const ImVec2 origin, const std::optional<double> time) {
    if (!time) {
        return;
    }
    const std::string text = std::format("t = {}s", Core::FormatValue(*time));
    const float margin = ImGui::GetFontSize() * INSTANT_MARGIN;
    draw_list->AddText({origin.x + margin, origin.y + margin}, ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
}

} // namespace GUI
