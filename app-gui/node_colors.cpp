/**
 * @file    node_colors.cpp
 * @brief   Colors that tell circuit nodes and currents apart, shared by the editor and the plots.
 */

#include "node_colors.h"

#include "implot.h"
#include <algorithm>
#include <array>

namespace GUI {

namespace {

// Ground (node 0) uses the first one, the others cycle through the rest
constexpr auto NodeColors = std::to_array<ImU32>({
    IM_COL32(160, 160, 160, 255),
    IM_COL32(230, 120, 100, 255),
    IM_COL32(110, 170, 240, 255),
    IM_COL32(240, 200, 90, 255),
    IM_COL32(190, 130, 230, 255),
    IM_COL32(100, 210, 190, 255),
    IM_COL32(240, 150, 200, 255),
});

// From a dim warm gray for the lowest values, through the accent red, to a pale yellow for the highest
constexpr auto HeatStops = std::to_array<ImVec4>({
    {96.0f / 255.0f, 80.0f / 255.0f, 84.0f / 255.0f, 1.0f},
    {156.0f / 255.0f, 42.0f / 255.0f, 49.0f / 255.0f, 1.0f},
    {236.0f / 255.0f, 96.0f / 255.0f, 100.0f / 255.0f, 1.0f},
    {255.0f / 255.0f, 214.0f / 255.0f, 120.0f / 255.0f, 1.0f},
});

} // namespace

/**
 * @brief   Returns the color of a node, so the same node looks the same in the editor and in the plots.
 * @param[in] node  Node number; 0 is ground.
 * @return  Gray for ground, and one of six colors for the other nodes, repeating after node 6.
 */
ImU32 GetNodeColor(const int node) {
    if (node == 0) {
        return NodeColors[0];
    }
    const auto cycle_length = static_cast<int>(NodeColors.size()) - 1;
    return NodeColors[1 + (node - 1) % cycle_length];
}

/**
 * @brief   Returns the color of a current, so the same current looks the same in the editor and in the plots.
 * @param[in] index  Position of the current among those the circuit reports, as Core::GetCurrentNames() lists
 *                   them part after part.
 * @return  A color of the ImPlot colormap, which stands apart from the node colors.
 * @note    Needs an ImPlot context.
 */
ImU32 GetCurrentColor(const std::size_t index) {
    return ImGui::ColorConvertFloat4ToU32(ImPlot::GetColormapColor(static_cast<int>(index)));
}

/**
 * @brief   Returns the color of a level on the heat scale the editor colors voltages and currents with.
 * @param[in] level  0 for the lowest value shown, 1 for the highest; values outside are clamped.
 * @return  A color from dim gray through red to pale yellow.
 */
ImU32 GetHeatColor(const float level) {
    const float position = std::clamp(level, 0.0f, 1.0f) * static_cast<float>(HeatStops.size() - 1);
    const auto stop = std::min(static_cast<std::size_t>(position), HeatStops.size() - 2);
    const float fraction = position - static_cast<float>(stop);
    const ImVec4 &low = HeatStops[stop];
    const ImVec4 &high = HeatStops[stop + 1];
    return ImGui::ColorConvertFloat4ToU32({low.x + (high.x - low.x) * fraction, low.y + (high.y - low.y) * fraction,
                                           low.z + (high.z - low.z) * fraction, 1.0f});
}

} // namespace GUI
