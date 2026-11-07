/**
 * @file    dock_layout.h
 * @brief   Keeps the proportions of the docked windows when the main window changes size.
 */

#ifndef IMCSIM_DOCK_LAYOUT_H
#define IMCSIM_DOCK_LAYOUT_H

#include "imgui.h"
#include "imgui_internal.h"

namespace GUI {

void ScaleDockLayout(ImGuiDockNode &node, ImVec2 ratio);
ImVec2 GetLaidOutSize(const ImGuiDockNode &root, ImVec2 followed_size);

} // namespace GUI

#endif // IMCSIM_DOCK_LAYOUT_H
