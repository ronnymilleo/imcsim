/**
 * @file    node_colors.cpp
 * @brief   Colors that tell circuit nodes apart, shared by the editor wires and the plots.
 */

#include "node_colors.h"

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

} // namespace GUI
