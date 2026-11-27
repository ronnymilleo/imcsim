/**
 * @file    selection_input.h
 * @brief   What the mouse and the keys do in the selecting mode, before the schematic is drawn.
 */

#ifndef IMCSIM_SELECTION_INPUT_H
#define IMCSIM_SELECTION_INPUT_H

#include "base/helpers.h"
#include "schematic/editor_command.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "windows/canvas_pointer.h"
#include "windows/part_popover.h"
#include <vector>

namespace GUI {

/**
 * @struct  SelectionInput
 * @brief   What the input of the selecting mode reads and changes, which the editor owns.
 */
struct SelectionInput {
    Schematic &Shown; ///< The schematic on show, whose items a click picks.
    EditorMode &Mode; ///< The mode of the editor, which holds the box and the drag while it is selecting.
    PartPopover &Popover;
    CanvasPointer Pointer; ///< Where the mouse is; it is not hovering when a live part took the mouse.
    /// ImGui closes a popup on Esc before the frame starts, so an Esc that closed one leaves the selection alone.
    bool WasPopupOpenLastFrame;
};

// Input
[[nodiscard]] std::vector<EditorCommand> HandleSelectionInput(const SelectionInput &input);

} // namespace GUI

#endif // IMCSIM_SELECTION_INPUT_H
