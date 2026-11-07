/**
 * @file    dock_layout.cpp
 * @brief   Keeps the proportions of the docked windows when the main window changes size.
 */

#include "windows/dock_layout.h"

namespace GUI {

/**
 * @brief   Scales the size every node of a dock tree asks for, so the windows keep their share of the main window when
 *          it grows or shrinks.
 * @details ImGui gives the whole change in size to the central node of a dockspace and keeps the others at their width
 *          in pixels, so a layout built in a small window, as when a tiling window manager resizes it right after it
 *          opens, would leave Output narrow beside a wide schematic. Scaling every node, splits the user dragged
 *          included, keeps the proportions instead.
 * @param[in,out] node   Root of the tree, usually the dockspace node.
 * @param[in]     ratio  New size of the main window over the old one, on each axis.
 */
void ScaleDockLayout(ImGuiDockNode &node, const ImVec2 ratio) {
    node.SizeRef.x *= ratio.x;
    node.SizeRef.y *= ratio.y;
    for (ImGuiDockNode *child : node.ChildNodes) {
        if (child != nullptr) {
            ScaleDockLayout(*child, ratio);
        }
    }
}

} // namespace GUI
