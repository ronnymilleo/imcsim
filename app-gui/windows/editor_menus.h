/**
 * @file    editor_menus.h
 * @brief   The items of the File and Edit menus that the editor draws in the main menu bar.
 */

#ifndef IMCSIM_EDITOR_MENUS_H
#define IMCSIM_EDITOR_MENUS_H

#include "schematic/editor_command.h"
#include "schematic/schematic.h"
#include <optional>

namespace GUI {

/**
 * @enum    BlockMenuChoice
 * @brief   The item of the block section of the Edit menu that was picked, if any.
 */
enum class BlockMenuChoice {
    NONE,
    CREATE_FROM_SELECTION,
    EDIT_CONTENTS,
    OPEN_SETTINGS,
    APPLY_AND_RETURN,
    DISCARD_CHANGES
};

// Items, which return the command picked, or none
std::optional<EditorCommand> DrawFileItems(bool is_disabled);
std::optional<EditorCommand> DrawEditItems(Schematic &schematic);
BlockMenuChoice DrawBlockItems(Schematic &schematic, bool is_editing_block);

} // namespace GUI

#endif // IMCSIM_EDITOR_MENUS_H
