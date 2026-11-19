/**
 * @file    editor_command.cpp
 * @brief   The commands of the editor, whichever menu, button or key asks for them, and what the turning ones ask of
 *          the selection.
 */

#include "schematic/editor_command.h"

#include <algorithm>
#include <array>

namespace GUI {

namespace {

/**
 * @struct  TurnCommand
 * @brief   A turning command and what it asks of the selection.
 */
struct TurnCommand {
    EditorCommand Command;
    TurnRequest Turn;
};

using Kind = PointTransform::Kind;

constexpr std::array TURN_COMMANDS = {
    TurnCommand{.Command = EditorCommand::ROTATE, .Turn = {.Kind = Kind::QUARTER_TURN, .IsKeepingWires = false}},
    TurnCommand{.Command = EditorCommand::MIRROR, .Turn = {.Kind = Kind::MIRROR_LEFT_RIGHT, .IsKeepingWires = false}},
    TurnCommand{.Command = EditorCommand::FLIP, .Turn = {.Kind = Kind::MIRROR_TOP_BOTTOM, .IsKeepingWires = false}},
    TurnCommand{.Command = EditorCommand::ROTATE_KEEP_WIRES,
                .Turn = {.Kind = Kind::QUARTER_TURN, .IsKeepingWires = true}},
    TurnCommand{.Command = EditorCommand::MIRROR_KEEP_WIRES,
                .Turn = {.Kind = Kind::MIRROR_LEFT_RIGHT, .IsKeepingWires = true}},
    TurnCommand{.Command = EditorCommand::FLIP_KEEP_WIRES,
                .Turn = {.Kind = Kind::MIRROR_TOP_BOTTOM, .IsKeepingWires = true}},
};

} // namespace

/**
 * @brief   Whether a command may run while the selection is being dragged.
 * @param[in] command  Any command.
 * @return  False for undoing, redoing, cutting and deleting, which remove or replace the elements that the drag keeps
 *          pointers into, so they are ignored until it ends; true for the others.
 */
bool CanRunDuringDrag(const EditorCommand command) {
    switch (command) {
    case EditorCommand::UNDO:
    case EditorCommand::REDO:
    case EditorCommand::CUT:
    case EditorCommand::DELETE_SELECTION:
        return false;
    default:
        return true;
    }
}

/**
 * @brief   What Rotate, Mirror and Flip, and their variants that keep the wires where they are, ask of the selection.
 * @param[in] command  Any command.
 * @return  The turn, or none for any other command.
 */
std::optional<TurnRequest> GetTurnRequest(const EditorCommand command) {
    const auto found = std::ranges::find(TURN_COMMANDS, command, &TurnCommand::Command);
    if (found == TURN_COMMANDS.end()) {
        return std::nullopt;
    }
    return found->Turn;
}

/**
 * @brief   The turning command that asks the selection for a turn, as the keys of the selecting mode do.
 * @param[in] turn  A quarter turn or a mirror, with or without the wires kept in place.
 * @return  The command, or none for a translation, which no command asks for.
 */
std::optional<EditorCommand> GetTurnCommand(const TurnRequest turn) {
    const auto found = std::ranges::find(TURN_COMMANDS, turn, &TurnCommand::Turn);
    if (found == TURN_COMMANDS.end()) {
        return std::nullopt;
    }
    return found->Command;
}

} // namespace GUI
