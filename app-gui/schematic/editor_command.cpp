/**
 * @file    editor_command.cpp
 * @brief   The commands of the File and Edit menus of the editor, and what the turning ones ask of the selection.
 */

#include "schematic/editor_command.h"

namespace GUI {

/**
 * @brief   What Rotate, Mirror and Flip, and their variants that keep the wires where they are, ask of the selection.
 * @return  The turn, or none for any other command.
 */
std::optional<TurnRequest> GetTurnRequest(const EditorCommand command) {
    switch (command) {
    case EditorCommand::ROTATE:
        return TurnRequest{.Kind = PointTransform::Kind::QUARTER_TURN, .IsKeepingWires = false};
    case EditorCommand::ROTATE_KEEP_WIRES:
        return TurnRequest{.Kind = PointTransform::Kind::QUARTER_TURN, .IsKeepingWires = true};
    case EditorCommand::MIRROR:
        return TurnRequest{.Kind = PointTransform::Kind::MIRROR_LEFT_RIGHT, .IsKeepingWires = false};
    case EditorCommand::MIRROR_KEEP_WIRES:
        return TurnRequest{.Kind = PointTransform::Kind::MIRROR_LEFT_RIGHT, .IsKeepingWires = true};
    case EditorCommand::FLIP:
        return TurnRequest{.Kind = PointTransform::Kind::MIRROR_TOP_BOTTOM, .IsKeepingWires = false};
    case EditorCommand::FLIP_KEEP_WIRES:
        return TurnRequest{.Kind = PointTransform::Kind::MIRROR_TOP_BOTTOM, .IsKeepingWires = true};
    default:
        return std::nullopt;
    }
}

} // namespace GUI
