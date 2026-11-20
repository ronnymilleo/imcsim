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
#include "windows/part_popover.h"
#include <vector>

namespace GUI {

/**
 * @struct  SelectionInput
 * @brief   What the input of the selecting mode reads and changes, which the editor owns.
 */
struct SelectionInput {
    /// The schematic on show, whose items a click picks.
    Schematic &Shown;
    /// The mode of the editor, which holds the box and the drag while it is selecting.
    EditorMode &Mode;
    PartPopover &Popover;
    const ViewTransform &View;
    /// Pixels per grid unit.
    float Zoom;
    /// Whether the cursor is over the canvas and no live part took the mouse.
    bool IsHovered;
    /// ImGui closes a popup on Esc before the frame starts, so an Esc that closed one leaves the selection alone.
    bool WasPopupOpenLastFrame;
};

// Input
[[nodiscard]] std::vector<EditorCommand> HandleSelectionInput(const SelectionInput &input);

} // namespace GUI

#endif // IMCSIM_SELECTION_INPUT_H
