/**
 * @file    node_colors.h
 * @brief   Colors that tell circuit nodes and currents apart, shared by the editor and the plots.
 */

#ifndef IMCSIM_NODE_COLORS_H
#define IMCSIM_NODE_COLORS_H

#include "imgui.h"
#include <cstddef>

namespace GUI {

ImU32 GetNodeColor(int node);
ImU32 GetCurrentColor(std::size_t index);
ImU32 GetHeatColor(float level);

} // namespace GUI

#endif // IMCSIM_NODE_COLORS_H
