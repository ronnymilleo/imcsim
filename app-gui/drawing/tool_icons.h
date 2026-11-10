/**
 * @file    tool_icons.h
 * @brief   Icons of the editor toolbar and of the part lists, drawn as lines on the window.
 */

#ifndef IMCSIM_TOOL_ICONS_H
#define IMCSIM_TOOL_ICONS_H

#include "components/component.h"
#include "imgui.h"
#include "ui_elements/ui_element.h"

namespace GUI {

/**
 * @enum    ToolIcon
 * @brief   Icons of the toolbar buttons that are not parts.
 */
enum class ToolIcon {
    RUN,
    STOP,
    SELECT,
    WIRE,
    PROBE,
    UNDO,
    REDO,
    FIT
};

// Sizes
float GetToolButtonSide();
float GetIconSize();
ImU32 GetIconColor(bool is_enabled);
ImVec2 GetItemCenter();

// Icons
void DrawToolIcon(ImDrawList *draw_list, ToolIcon icon, ImVec2 center, float size, ImU32 color);
void DrawPartIcon(ImDrawList *draw_list, Core::ComponentType type, ImVec2 center, float size, SymbolStyle style);

// Part lists
bool SelectablePartRow(Core::ComponentType type, bool is_selected, float width, SymbolStyle style);

} // namespace GUI

#endif // IMCSIM_TOOL_ICONS_H
