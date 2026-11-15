/**
 * @file    mode_input.h
 * @brief   What the mouse and the keys do in each mode of the editor once the schematic is drawn.
 */

#ifndef IMCSIM_MODE_INPUT_H
#define IMCSIM_MODE_INPUT_H

#include "base/helpers.h"
#include "canvas/schematic_canvas.h"
#include "overlays/schematic_overlays.h"
#include "schematic/block_session.h"
#include "schematic/editor_mode.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file_actions.h"
#include "windows/part_editor.h"
#include "windows/part_popover.h"
#include <optional>

namespace GUI {

/**
 * @struct  ModeInput
 * @brief   What the input of a mode reads and changes, which the editor owns: the schematic on show, where the mouse
 *          is, and the parts of the editor a mode starts or asks something of.
 */
struct ModeInput {
    /// The document, which holds the blocks a part may place.
    Schematic &Root;
    /// The open blocks, whose shown schematic is where parts and groups land.
    BlockSession &Blocks;
    SchematicOverlays &Overlays;
    PartEditor &Part;
    PartPopover &Popover;
    SchematicFileActions &Files;
    const Preferences &UserPreferences;
    SchematicCanvas &Canvas;
    const ViewTransform &View;
    /// Pixels per grid unit.
    float Zoom;
    /// Whether the cursor is over the canvas.
    bool IsHovered;
    /// Whether wires go up or down before they turn, kept from one wire to the next.
    bool &IsWireVerticalFirst;
};

// Input of each mode, after the schematic is drawn; each returns the mode to switch to, or none
std::optional<EditorMode> HandleMode(SelectingMode &mode, const ModeInput &input);
std::optional<EditorMode> HandleMode(PlacingMode &mode, const ModeInput &input);
std::optional<EditorMode> HandleMode(WiringMode &mode, const ModeInput &input);
std::optional<EditorMode> HandleMode(ProbingMode &mode, const ModeInput &input);
std::optional<EditorMode> HandleMode(PastingMode &mode, const ModeInput &input);

} // namespace GUI

#endif // IMCSIM_MODE_INPUT_H
