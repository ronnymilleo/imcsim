/**
 * @file    editor_mode.cpp
 * @brief   What the editor is doing, one thing at a time: selecting, placing a part, drawing wires, probing or pasting.
 */

#include "editing/editor_mode.h"

#include "editing/element_factory.h"
#include <format>
#include <utility>

namespace GUI {

/**
 * @brief   Tells whether clicks select: no part is being placed, no wire drawn, nothing probed or pasted.
 * @param[in] mode  The mode of the editor.
 * @return  True in the selecting mode.
 */
bool IsSelecting(const EditorMode &mode) {
    return std::holds_alternative<SelectingMode>(mode);
}

/**
 * @brief   The part being placed, which the toolbar shows as the tool in use.
 * @param[in] mode  The mode of the editor.
 * @return  The type of the part, or no value outside the placing mode.
 */
std::optional<Core::ComponentType> GetPlacingType(const EditorMode &mode) {
    if (const auto *placing = std::get_if<PlacingMode>(&mode)) {
        return placing->Type;
    }
    return std::nullopt;
}

/**
 * @brief   What a mode does and which keys it takes, for the status bar, so the shortcuts can be learned while working.
 * @param[in] mode  The mode of the editor.
 * @return  The hint of the mode, or no value while selecting, whose hint depends on what is selected
 *          (DescribeSelecting()).
 */
std::optional<std::string> DescribeMode(const EditorMode &mode) {
    if (std::holds_alternative<PastingMode>(mode)) {
        return "Pasting: click to place, R rotate, M mirror, Shift+M flip, Esc or right click to cancel";
    }
    if (const auto *placing = std::get_if<PlacingMode>(&mode)) {
        return std::format("Placing {}: click to place, R rotate, M mirror, Shift+M flip, Esc or right click to stop",
                           GetPartName(placing->Type));
    }
    if (const auto *wiring = std::get_if<WiringMode>(&mode)) {
        return wiring->Start ? "Wire: click to bend, a terminal or the same point twice to end, F flip the bend, Esc "
                               "to stop"
                             : "Wire: click to start, Esc or right click to stop";
    }
    if (std::holds_alternative<ProbingMode>(mode)) {
        return "Probe: click a wire for its voltage or a part for its current, Esc or right click to stop";
    }
    return std::nullopt;
}

/**
 * @brief   What the clicks and keys of the selecting mode do now, for the status bar.
 * @param[in] is_live         Whether a live run goes on, during which a click sets parts instead.
 * @param[in] selected_part   Name of the one part selected alone, or no value.
 * @param[in] selected_count  How many parts and wires are selected.
 * @return  The hint for a live run, for one part, for one wire, for several items, or for an empty selection.
 */
std::string DescribeSelecting(const bool is_live, const std::optional<std::string> &selected_part,
                              const std::size_t selected_count) {
    if (is_live) {
        return "Live: click a switch or logic input to flip it, hold a push button down, turn a potentiometer with the "
               "wheel; any other edit ends the run";
    }
    if (selected_part) {
        return std::format(
            "{} selected: Enter or double click to edit, type a value, drag to move, R rotate, M mirror, "
            "Alt keeps the wires, Del delete, right click for more",
            *selected_part);
    }
    if (selected_count == 1) {
        return "Wire selected: drag to slide it, double click for its whole node, Del delete, Esc deselect";
    }
    if (selected_count > 1) {
        return std::format(
            "{} items selected: drag to move, R rotate, M mirror, Alt keeps the wires, Shift+click to add "
            "or remove, Del delete, Esc deselect",
            selected_count);
    }
    return "Click a part or wire to select it, drag to select an area, middle drag to pan, wheel to zoom, Space find a "
           "part, W wire, P probe";
}

/**
 * @brief   Turns the part being placed a quarter turn, as R does.
 * @param[in,out] mode  The placing mode.
 */
void TurnPlacement(PlacingMode &mode) {
    mode.Orientation = NextRotation(mode.Orientation);
}

/**
 * @brief   Mirrors the part being placed, as M does, or flips it top to bottom, as Shift+M does.
 * @param[in,out] mode              The placing mode.
 * @param[in]     is_top_to_bottom  True to flip top to bottom, which is a mirror and a half turn.
 */
void MirrorPlacement(PlacingMode &mode, const bool is_top_to_bottom) {
    mode.IsMirrored = !mode.IsMirrored;
    if (is_top_to_bottom) {
        mode.Orientation = NextRotation(NextRotation(mode.Orientation));
    }
}

/**
 * @brief   Where a wire from the start to the cursor bends, as it is drawn in two straight pieces.
 * @param[in] start              Where the wire starts.
 * @param[in] cursor             Where it ends.
 * @param[in] is_vertical_first  True to go up or down first, as F toggles.
 * @return  The corner between the two pieces.
 */
GridPoint FindWireCorner(const GridPoint start, const GridPoint cursor, const bool is_vertical_first) {
    return is_vertical_first ? GridPoint{start.X, cursor.Y} : GridPoint{cursor.X, start.Y};
}

/**
 * @brief   A click in wire mode: the first starts a wire, each next one adds a bend up to the cursor and goes on from
 *          there; a click on a terminal, or a second one at the same point, ends the wire.
 * @param[in,out] mode               The wiring mode, whose start moves with the clicks.
 * @param[in]     cursor             The grid point clicked.
 * @param[in]     is_vertical_first  True when the wire goes up or down before it turns.
 * @param[in]     is_on_terminal     Whether the point clicked is a terminal of a part.
 * @return  The two pieces to add to the schematic, or none for the first click and for one that only ends the wire.
 */
std::vector<UIWire> ClickWire(WiringMode &mode, const GridPoint cursor, const bool is_vertical_first,
                              const bool is_on_terminal) {
    if (!mode.Start) {
        mode.Start = cursor;
        return {};
    }
    const GridPoint start = *mode.Start;
    if (cursor == start) {
        mode.Start.reset();
        return {};
    }
    const GridPoint corner = FindWireCorner(start, cursor, is_vertical_first);
    mode.Start = is_on_terminal ? std::nullopt : std::optional(cursor);
    return {UIWire(start, corner), UIWire(corner, cursor)};
}

/**
 * @brief   Esc and right click first drop the wire in progress, and only leave wire mode when there is none.
 * @param[in,out] mode  The wiring mode.
 * @return  True when the editor should leave wire mode.
 */
bool StopWire(WiringMode &mode) {
    return !std::exchange(mode.Start, std::nullopt).has_value();
}

} // namespace GUI
