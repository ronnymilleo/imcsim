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

/**
 * @brief   The size of the main window the layout was laid out for, which a change in size is scaled from.
 * @param[in] root           Root of the tree, the dockspace node.
 * @param[in] followed_size  The size the layout last followed, or zero before the first frame.
 * @return  The followed size, or, on the first frame, the size the root node was saved with in imgui.ini, so a window
 *          that opens at another size before the window manager resizes it scales the saved layout from its own size.
 */
ImVec2 GetLaidOutSize(const ImGuiDockNode &root, const ImVec2 followed_size) {
    if (followed_size.x > 0.0f && followed_size.y > 0.0f) {
        return followed_size;
    }
    return root.Size;
}

} // namespace GUI
