/**
 * @file    editor_command.h
 * @brief   The commands of the editor, whichever menu, button or key asks for them, and what the turning ones ask of
 *          the selection.
 */

#ifndef IMCSIM_EDITOR_COMMAND_H
#define IMCSIM_EDITOR_COMMAND_H

#include "schematic/wire_editing.h"
#include <optional>

namespace GUI {

/**
 * @enum    EditorCommand
 * @brief   A command of the editor, asked for by a menu item, a button of the command bar, an item of the context menu
 *          or a shortcut; the editor window carries each one out in one place.
 */
enum class EditorCommand {
    // File
    NEW,
    OPEN,
    SAVE,
    SAVE_AS,
    IMPORT,
    IMPORT_SPICE,
    EXPORT_SCHEMATIC,
    QUIT,
    /// Runs the selected analysis or continues a stopped live run, as F5 does; it never stops a run.
    RUN,
    // History
    UNDO,
    REDO,
    // Turning the selection
    ROTATE,
    MIRROR,
    FLIP,
    ROTATE_KEEP_WIRES,
    MIRROR_KEEP_WIRES,
    FLIP_KEEP_WIRES,
    // Clipboard and selection
    CUT,
    COPY,
    PASTE,
    SELECT_ALL,
    DELETE_SELECTION,
    // View
    FIT,
    // Blocks
    CREATE_BLOCK,
    EDIT_BLOCK,
    OPEN_BLOCK_SETTINGS,
    APPLY_BLOCK,
    DISCARD_BLOCK
};

/**
 * @struct  TurnRequest
 * @brief   How a turning command moves the selected parts, and whether the wires stay where they are.
 */
struct TurnRequest {
    PointTransform::Kind Kind;
    bool IsKeepingWires;

    bool operator==(const TurnRequest &) const = default;
};

// Dragging
bool CanRunDuringDrag(EditorCommand command);

// Turning
[[nodiscard]] std::optional<TurnRequest> GetTurnRequest(EditorCommand command);
[[nodiscard]] std::optional<EditorCommand> GetTurnCommand(TurnRequest turn);

} // namespace GUI

#endif // IMCSIM_EDITOR_COMMAND_H
