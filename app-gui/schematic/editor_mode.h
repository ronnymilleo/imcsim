/**
 * @file    editor_mode.h
 * @brief   What the editor is doing, one thing at a time: selecting, placing a part, drawing wires, probing or pasting.
 */

#ifndef IMCSIM_EDITOR_MODE_H
#define IMCSIM_EDITOR_MODE_H

#include "base/helpers.h"
#include "circuit/components/component.h"
#include "imgui.h"
#include "schematic/clipboard.h"
#include "schematic/selection.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace GUI {

/**
 * @struct  GroupDrag
 * @brief   Schematic state captured when a drag of the selection starts; every frame of the drag is computed from it.
 * @details A single wire only slides across itself, so SlidesVertically tells which way it may go.
 */
struct GroupDrag {
    GridPoint StartCursor;
    std::set<std::size_t> Elements;
    std::set<std::size_t> Wires;
    std::vector<GridPoint> StartPositions;
    std::vector<GridPoint> StartTerminals;
    std::vector<GridPoint> FixedTerminals;
    std::vector<UIWire> StartWires;
    std::optional<bool> SlidesVertically;
    /// Moved the mouse far enough to change something.
    bool Moved = false;
    GridPoint LastCursor;
    GridPoint LastDelta;
    /// What a click without a move leaves selected.
    std::optional<Selection> CollapseTo;
};

/**
 * @struct  SelectionBox
 * @brief   A selection box being dragged on empty canvas: where it started, and what was selected before it.
 */
struct SelectionBox {
    ImVec2 Start;
    Selection Base;
};

/**
 * @struct  SelectingMode
 * @brief   The default mode, where clicks select, drag and open the properties of what they land on.
 */
struct SelectingMode {
    std::optional<GroupDrag> Drag;
    std::optional<SelectionBox> Box;
};

/**
 * @struct  PlacingMode
 * @brief   A part follows the cursor, and each click places a copy of it.
 * @details A block is placed as a part of type SUBCIRCUIT, with Block naming its definition.
 */
struct PlacingMode {
    Core::ComponentType Type = Core::ComponentType::RESISTOR;
    std::string Block;
    Rotation Orientation = Rotation::R0;
    bool IsMirrored = false;
};

/**
 * @struct  WiringMode
 * @brief   Clicks draw wires with a bend; Start is where the wire in progress began, or none before the first click.
 */
struct WiringMode {
    std::optional<GridPoint> Start;
};

/**
 * @struct  ProbingMode
 * @brief   Clicks pick what the plots measure: a wire for its voltage, a part for its current.
 */
struct ProbingMode {};

/**
 * @struct  PastingMode
 * @brief   What was read from the clipboard or a file follows the cursor until a click places it.
 */
struct PastingMode {
    PastedGroup Group;
};

/// The one mode the editor is in; starting another leaves it.
using EditorMode = std::variant<SelectingMode, PlacingMode, WiringMode, ProbingMode, PastingMode>;

// Modes
bool IsSelecting(const EditorMode &mode);
std::optional<Core::ComponentType> GetPlacingType(const EditorMode &mode);

// Status hints
std::optional<std::string> DescribeMode(const EditorMode &mode);
std::string DescribeSelecting(bool is_live, const std::optional<std::string> &selected_part,
                              std::size_t selected_count);

// Placing
void TurnPlacement(PlacingMode &mode);
void MirrorPlacement(PlacingMode &mode, bool is_top_to_bottom);

// Wiring
GridPoint FindWireCorner(GridPoint start, GridPoint cursor, bool is_vertical_first);
std::vector<UIWire> ClickWire(WiringMode &mode, GridPoint cursor, bool is_vertical_first, bool is_on_terminal);
bool StopWire(WiringMode &mode);

} // namespace GUI

#endif // IMCSIM_EDITOR_MODE_H
