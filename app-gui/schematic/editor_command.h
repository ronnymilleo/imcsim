/**
 * @file    editor_command.h
 * @brief   The commands of the File and Edit menus of the editor, and what the turning ones ask of the selection.
 */

#ifndef IMCSIM_EDITOR_COMMAND_H
#define IMCSIM_EDITOR_COMMAND_H

#include "schematic/wire_editing.h"
#include <optional>

namespace GUI {

/**
 * @enum    EditorCommand
 * @brief   A command picked in a menu, run by the next Update() so its popups belong to the editor window.
 */
enum class EditorCommand {
    NEW,
    OPEN,
    SAVE,
    SAVE_AS,
    IMPORT,
    IMPORT_SPICE,
    EXPORT_SCHEMATIC,
    UNDO,
    REDO,
    ROTATE,
    MIRROR,
    FLIP,
    ROTATE_KEEP_WIRES,
    MIRROR_KEEP_WIRES,
    FLIP_KEEP_WIRES,
    CUT,
    COPY,
    PASTE,
    SELECT_ALL,
    DELETE_SELECTION
};

/**
 * @struct  TurnRequest
 * @brief   How a turning command moves the selected parts, and whether the wires stay where they are.
 */
struct TurnRequest {
    PointTransform::Kind Kind;
    bool IsKeepingWires;
};

// Turning
std::optional<TurnRequest> GetTurnRequest(EditorCommand command);

} // namespace GUI

#endif // IMCSIM_EDITOR_COMMAND_H
