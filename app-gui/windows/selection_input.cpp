/**
 * @file    selection_input.cpp
 * @brief   What the mouse and the keys do in the selecting mode, before the schematic is drawn.
 */

#include "windows/selection_input.h"

#include "imgui.h"
#include "schematic/selection.h"
#include "schematic/selection_editing.h"
#include "schematic/wire_editing.h"
#include "windows/turn_keys.h"
#include <optional>

namespace GUI {

namespace {

/**
 * @brief   A click picks what is under the cursor: empty canvas starts a box, an item selects it or, with Shift,
 *          toggles it, and an item of a selected group keeps the group so it can be dragged.
 */
void HandleClick(const SelectionInput &input, SelectingMode &selecting, const ImVec2 cursor_world) {
    Schematic &schematic = input.Shown;
    const bool shift = ImGui::GetIO().KeyShift;
    const Selection picked =
        PickAt(schematic.GetElements(), schematic.GetWires(), cursor_world, input.Pointer.GetPickTolerance());
    const Selection current = schematic.GetSelection();
    if (picked.Empty()) {
        // Empty canvas starts a box; with Shift it adds to what is selected, otherwise it starts over
        if (!shift) {
            CancelSelection(schematic, input.Mode);
        }
        selecting.Box = SelectionBox{.Start = cursor_world, .Base = shift ? current : Selection{}};
    } else if (shift) {
        ApplySelection(schematic, input.Mode, Toggled(current, picked));
    } else if (Joined(current, picked) == current) {
        // Part of a group: keep the group so it can be dragged, and collapse to the item if it is only a click
        selecting.Drag = MakeDrag(schematic, Snap(cursor_world), picked);
    } else {
        ApplySelection(schematic, input.Mode, picked);
        selecting.Drag = MakeDrag(schematic, Snap(cursor_world), std::nullopt);
    }
}

/**
 * @brief   A double click opens the properties of the selected part, or selects the whole node of a wire.
 * @return  The command that opens the contents of a block, when the part is one; none otherwise.
 */
std::optional<EditorCommand> HandleDoubleClick(const SelectionInput &input) {
    Schematic &schematic = input.Shown;
    if (schematic.GetSelectedElement() != nullptr) {
        // Ending the drag of a click on a group collapses the selection to the part clicked
        EndDrag(schematic, input.Mode);
        if (schematic.GetSelectedElement()->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
            return EditorCommand::EDIT_BLOCK;
        }
        input.Popover.Open();
    } else if (schematic.GetSelection().Elements.empty() && schematic.GetSelection().Wires.size() == 1) {
        const Selection node =
            PickNode(schematic.GetWires(), schematic.GetConnectivity(), *schematic.GetSelection().Wires.begin());
        EndDrag(schematic, input.Mode);
        ApplySelection(schematic, input.Mode, node);
    }
    return std::nullopt;
}

/**
 * @brief   While the mouse button is down the selection box takes in what it touches; it ends when the button comes up.
 */
void UpdateSelectionBox(const SelectionInput &input, SelectingMode &selecting, const ImVec2 cursor_world) {
    if (!selecting.Box) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Selection inside =
            PickInRectangle(input.Shown.GetElements(), input.Shown.GetWires(), selecting.Box->Start, cursor_world);
        ApplySelection(input.Shown, input.Mode, Joined(selecting.Box->Base, inside));
    } else {
        selecting.Box.reset();
    }
}

/**
 * @brief   While the mouse button is down the drag follows the cursor; it ends when the button comes up.
 */
void UpdateSelectionDrag(const SelectionInput &input, const SelectingMode &selecting, const ImVec2 cursor_world) {
    if (!selecting.Drag) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        UpdateDrag(input.Shown, input.Mode, Snap(cursor_world));
    } else {
        EndDrag(input.Shown, input.Mode);
    }
}

/**
 * @brief   R rotates the selection, M mirrors it, Enter opens its properties, Delete removes it and Esc clears it.
 * @details R, M and Delete ask for the commands of the Edit menu; they work whatever other modifier is held, which a
 *          shortcut of the table would not, and holding Alt keeps the wires where they are.
 * @return  The commands the keys ask for, in the order they were read.
 */
std::vector<EditorCommand> HandleKeys(const SelectionInput &input) {
    std::vector<EditorCommand> commands;
    if (const std::optional<PointTransform::Kind> turn = ReadTurnKey()) {
        if (const std::optional<EditorCommand> command =
                GetTurnCommand({.Kind = *turn, .IsKeepingWires = ImGui::GetIO().KeyAlt})) {
            commands.push_back(*command);
        }
    }
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter && input.Shown.GetSelectedElement() != nullptr && !ImGui::IsAnyItemActive()) {
        input.Popover.Open();
    }
    // On macOS compact keyboards the key labeled Delete sends Backspace
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        commands.push_back(EditorCommand::DELETE_SELECTION);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !input.WasPopupOpenLastFrame) {
        CancelSelection(input.Shown, input.Mode);
    }
    return commands;
}

} // namespace

/**
 * @brief   Handles the selecting mode: a click selects an element or a wire, Shift+click adds or removes one, a drag on
 *          empty canvas selects what the box touches, and a double click on a wire selects its whole node. Dragging the
 *          selection moves it, a single wire sliding across itself, with the wires around it following. R rotates the
 *          selection, M mirrors it left to right and Shift+M top to bottom, both about its center, and holding Alt
 *          leaves the wires where they are; Delete removes the selection and Esc clears it.
 * @param[in] input  The schematic on show, the mode and where the mouse is.
 * @return  The commands the clicks and keys ask the editor for, such as turning the selection or opening a block, in
 *          the order they were asked; none outside the selecting mode.
 * @note    Call it before the schematic is drawn, so a dragged element is drawn where the cursor is this frame.
 */
std::vector<EditorCommand> HandleSelectionInput(const SelectionInput &input) {
    auto *selecting = std::get_if<SelectingMode>(&input.Mode);
    if (selecting == nullptr) {
        return {};
    }
    std::vector<EditorCommand> commands;
    const ImVec2 cursor_world = input.Pointer.GetWorldPosition();
    if (input.Pointer.IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        HandleClick(input, *selecting, cursor_world);
    }
    if (input.Pointer.IsHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (const std::optional<EditorCommand> command = HandleDoubleClick(input)) {
            commands.push_back(*command);
        }
    }
    UpdateSelectionBox(input, *selecting, cursor_world);
    UpdateSelectionDrag(input, *selecting, cursor_world);
    if (ImGui::IsWindowFocused()) {
        const std::vector<EditorCommand> asked = HandleKeys(input);
        commands.insert(commands.end(), asked.begin(), asked.end());
    }
    return commands;
}

} // namespace GUI
