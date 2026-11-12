/**
 * @file    editor_menus.cpp
 * @brief   The items of the File and Edit menus that the editor draws in the main menu bar.
 */

#include "windows/editor_menus.h"

#include "imgui.h"
#include "schematic/clipboard.h"
#include "schematic/shortcuts.h"

namespace GUI {

namespace {

/**
 * @brief   Rotate, mirror and flip the selected parts, with or without their wires.
 */
void DrawTurnItems(const bool part_selected, std::optional<EditorCommand> &command) {
    if (ImGui::MenuItem("Rotate", "R", false, part_selected)) {
        command = EditorCommand::ROTATE;
    }
    if (ImGui::MenuItem("Mirror", "M", false, part_selected)) {
        command = EditorCommand::MIRROR;
    }
    if (ImGui::MenuItem("Flip", "Shift+M", false, part_selected)) {
        command = EditorCommand::FLIP;
    }
    if (ImGui::BeginMenu("Keep Wires in Place", part_selected)) {
        if (ImGui::MenuItem("Rotate", "Alt+R")) {
            command = EditorCommand::ROTATE_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Mirror", "Alt+M")) {
            command = EditorCommand::MIRROR_KEEP_WIRES;
        }
        if (ImGui::MenuItem("Flip", "Alt+Shift+M")) {
            command = EditorCommand::FLIP_KEEP_WIRES;
        }
        ImGui::EndMenu();
    }
}

} // namespace

/**
 * @brief   Draws the File menu items that act on the schematic, with their shortcuts.
 * @param[in] is_disabled  Whether the items are grayed out, as while a block is open.
 * @return  The command picked, or none; it runs on the next frame of the editor.
 * @note    Call between BeginMenu() and EndMenu().
 */
std::optional<EditorCommand> DrawFileItems(const bool is_disabled) {
    std::optional<EditorCommand> command;
    ImGui::BeginDisabled(is_disabled);
    if (ImGui::MenuItem("New", IMCSIM_SHORTCUT "N")) {
        command = EditorCommand::NEW;
    }
    if (ImGui::MenuItem("Open...", IMCSIM_SHORTCUT "O")) {
        command = EditorCommand::OPEN;
    }
    if (ImGui::MenuItem("Save", IMCSIM_SHORTCUT "S")) {
        command = EditorCommand::SAVE;
    }
    if (ImGui::MenuItem("Save As...", IMCSIM_SHORTCUT "Shift+S")) {
        command = EditorCommand::SAVE_AS;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import...")) {
        command = EditorCommand::IMPORT;
    }
    ImGui::SetItemTooltip("Add the circuit of a file to this schematic: it follows the cursor until a click places it");
    if (ImGui::MenuItem("Import SPICE Library...")) {
        command = EditorCommand::IMPORT_SPICE;
    }
    ImGui::SetItemTooltip("Bring in a vendor's subcircuits, such as op-amp models, as blocks kept in this schematic");
    if (ImGui::MenuItem("Export Schematic...")) {
        command = EditorCommand::EXPORT_SCHEMATIC;
    }
    ImGui::SetItemTooltip("Save the schematic as an SVG image, to show next to its plots");
    ImGui::EndDisabled();
    return command;
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @param[in,out] schematic  Schematic on show, whose history and selection say what applies.
 * @return  The command picked, or none; it runs on the next frame of the editor.
 * @note    Call between BeginMenu() and EndMenu(), before DrawBlockItems().
 */
std::optional<EditorCommand> DrawEditItems(Schematic &schematic) {
    std::optional<EditorCommand> command;
    if (ImGui::MenuItem("Undo", IMCSIM_SHORTCUT "Z", false, schematic.CanUndo())) {
        command = EditorCommand::UNDO;
    }
    if (ImGui::MenuItem("Redo", IMCSIM_SHORTCUT "Y", false, schematic.CanRedo())) {
        command = EditorCommand::REDO;
    }
    ImGui::Separator();
    const bool part_selected = !schematic.GetSelectedElements().empty();
    DrawTurnItems(part_selected, command);
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", IMCSIM_SHORTCUT "X", false, schematic.HasSelection())) {
        command = EditorCommand::CUT;
    }
    if (ImGui::MenuItem("Copy", IMCSIM_SHORTCUT "C", false, schematic.HasSelection())) {
        command = EditorCommand::COPY;
    }
    const char *clipboard_text = ImGui::GetClipboardText();
    const bool can_paste = clipboard_text != nullptr && LooksLikeCopiedSchematic(clipboard_text);
    if (ImGui::MenuItem("Paste", IMCSIM_SHORTCUT "V", false, can_paste)) {
        command = EditorCommand::PASTE;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Select All", IMCSIM_SHORTCUT "A")) {
        command = EditorCommand::SELECT_ALL;
    }
    if (ImGui::MenuItem("Delete", "Del", false, schematic.HasSelection())) {
        command = EditorCommand::DELETE_SELECTION;
    }
    ImGui::Separator();
    return command;
}

/**
 * @brief   Making a block from the selection, opening the selected block, and, inside a block, its settings and the
 *          ways out.
 * @param[in,out] schematic        Schematic on show, whose selection says what applies.
 * @param[in]     is_editing_block  Whether a block is open.
 * @return  The item picked, or none.
 */
BlockMenuChoice DrawBlockItems(Schematic &schematic, const bool is_editing_block) {
    BlockMenuChoice choice = BlockMenuChoice::NONE;
    if (!is_editing_block &&
        ImGui::MenuItem("Create Block from Selection...", nullptr, false, schematic.HasSelection())) {
        choice = BlockMenuChoice::CREATE_FROM_SELECTION;
    }
    const UIElement *selected = schematic.GetSelectedElement();
    const bool block_selected =
        selected != nullptr && selected->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT;
    if (ImGui::MenuItem("Edit Block Contents", nullptr, false, block_selected)) {
        choice = BlockMenuChoice::EDIT_CONTENTS;
    }
    if (!is_editing_block) {
        return choice;
    }
    if (ImGui::MenuItem("Block Pin and Parameter Settings...")) {
        choice = BlockMenuChoice::OPEN_SETTINGS;
    }
    if (ImGui::MenuItem("Apply Block and Return")) {
        choice = BlockMenuChoice::APPLY_AND_RETURN;
    }
    if (ImGui::MenuItem("Discard Block Changes")) {
        choice = BlockMenuChoice::DISCARD_CHANGES;
    }
    return choice;
}

} // namespace GUI
