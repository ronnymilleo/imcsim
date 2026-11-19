/**
 * @file    editor_menus.h
 * @brief   The items of the File and Edit menus that the editor draws in the main menu bar, and the turning items its
 *          context menu shares.
 */

#ifndef IMCSIM_EDITOR_MENUS_H
#define IMCSIM_EDITOR_MENUS_H

#include "schematic/editor_command.h"
#include "schematic/schematic.h"
#include <optional>

namespace GUI {

/**
 * @class   CommandItems
 * @brief   Menu items that each ask for a command, labeled with its keys, and the one picked this frame.
 */
class CommandItems {
public:
    void Draw(const char *label, EditorCommand command, bool is_enabled = true);
    std::optional<EditorCommand> GetPicked() const;

private:
    /// The command of the item clicked this frame, or none.
    std::optional<EditorCommand> m_Picked;
};

// Items, which return the command picked, or none
std::optional<EditorCommand> DrawFileItems(bool is_disabled);
std::optional<EditorCommand> DrawEditItems(Schematic &schematic);
void DrawTurnItems(CommandItems &items, bool is_enabled);
std::optional<EditorCommand> DrawBlockItems(Schematic &schematic, bool is_editing_block);

} // namespace GUI

#endif // IMCSIM_EDITOR_MENUS_H
