/**
 * @file    node_colors.h
 * @brief   Colors that tell circuit nodes apart, shared by the editor wires and the plots.
 */

#ifndef IMCSIM_NODE_COLORS_H
#define IMCSIM_NODE_COLORS_H

#include "imgui.h"

namespace GUI {

ImU32 GetNodeColor(int node);

} // namespace GUI

#endif // IMCSIM_NODE_COLORS_H
