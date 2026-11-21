/**
 * @file    editor_menus.cpp
 * @brief   The items of the File and Edit menus that the editor draws in the main menu bar, and the turning items its
 *          context menu shares.
 */

#include "windows/editor_menus.h"

#include "imgui.h"
#include "schematic/clipboard.h"
#include "schematic/shortcuts.h"

namespace GUI {

/**
 * @brief   Draws a menu item that asks for a command, with the keys of the command beside it.
 * @param[in] label       Text of the item.
 * @param[in] command     What the item asks for.
 * @param[in] is_enabled  Whether the item can be clicked.
 * @note    The item is the last one drawn, so a tooltip set right after it belongs to it.
 */
void CommandItems::Draw(const char *label, const EditorCommand command, const bool is_enabled) {
    if (ImGui::MenuItem(label, GetShortcutLabel(command), false, is_enabled)) {
        m_Picked = command;
    }
}

/**
 * @brief   The command of the item clicked this frame.
 * @return  The command, or none when no item was clicked.
 */
std::optional<EditorCommand> CommandItems::GetPicked() const {
    return m_Picked;
}

/**
 * @brief   Draws the File menu items that act on the schematic, with their shortcuts.
 * @param[in] is_disabled  Whether the items are grayed out, as while a block is open.
 * @return  The command picked, or none; it runs on the next frame of the editor.
 * @note    Call between BeginMenu() and EndMenu().
 */
std::optional<EditorCommand> DrawFileItems(const bool is_disabled) {
    CommandItems items;
    ImGui::BeginDisabled(is_disabled);
    items.Draw("New", EditorCommand::NEW);
    items.Draw("Open...", EditorCommand::OPEN);
    items.Draw("Save", EditorCommand::SAVE);
    items.Draw("Save As...", EditorCommand::SAVE_AS);
    ImGui::Separator();
    items.Draw("Import...", EditorCommand::IMPORT);
    ImGui::SetItemTooltip("Add the circuit of a file to this schematic: it follows the cursor until a click places it");
    items.Draw("Import SPICE Library...", EditorCommand::IMPORT_SPICE);
    ImGui::SetItemTooltip("Bring in a vendor's subcircuits, such as op-amp models, as blocks kept in this schematic");
    items.Draw("Export Schematic...", EditorCommand::EXPORT_SCHEMATIC);
    ImGui::SetItemTooltip("Save the schematic as an SVG image, to show next to its plots");
    ImGui::EndDisabled();
    return items.GetPicked();
}

/**
 * @brief   Draws the Edit menu items, with their shortcuts, enabled only when they apply.
 * @param[in,out] schematic  Schematic on show, whose history and selection say what applies.
 * @return  The command picked, or none; it runs on the next frame of the editor.
 * @note    Call between BeginMenu() and EndMenu(), before DrawBlockItems().
 */
std::optional<EditorCommand> DrawEditItems(Schematic &schematic) {
    CommandItems items;
    items.Draw("Undo", EditorCommand::UNDO, schematic.CanUndo());
    items.Draw("Redo", EditorCommand::REDO, schematic.CanRedo());
    ImGui::Separator();
    DrawTurnItems(items, !schematic.GetSelection().Elements.empty());
    ImGui::Separator();
    items.Draw("Cut", EditorCommand::CUT, schematic.HasSelection());
    items.Draw("Copy", EditorCommand::COPY, schematic.HasSelection());
    const char *clipboard_text = ImGui::GetClipboardText();
    items.Draw("Paste", EditorCommand::PASTE, clipboard_text != nullptr && LooksLikeCopiedSchematic(clipboard_text));
    ImGui::Separator();
    items.Draw("Select All", EditorCommand::SELECT_ALL);
    items.Draw("Delete", EditorCommand::DELETE_SELECTION, schematic.HasSelection());
    ImGui::Separator();
    return items.GetPicked();
}

/**
 * @brief   Draws Rotate, Mirror and Flip for the selected parts, and their variants that keep the wires in place, as
 *          the Edit menu and the context menu both show them.
 * @param[in,out] items       The items of the menu, which take the command picked.
 * @param[in]     is_enabled  Whether parts are selected.
 */
void DrawTurnItems(CommandItems &items, const bool is_enabled) {
    items.Draw("Rotate", EditorCommand::ROTATE, is_enabled);
    items.Draw("Mirror", EditorCommand::MIRROR, is_enabled);
    items.Draw("Flip", EditorCommand::FLIP, is_enabled);
    if (ImGui::BeginMenu("Keep Wires in Place", is_enabled)) {
        items.Draw("Rotate", EditorCommand::ROTATE_KEEP_WIRES);
        items.Draw("Mirror", EditorCommand::MIRROR_KEEP_WIRES);
        items.Draw("Flip", EditorCommand::FLIP_KEEP_WIRES);
        ImGui::EndMenu();
    }
}

/**
 * @brief   Making a block from the selection, opening the selected block, and, inside a block, its settings and the
 *          ways out.
 * @param[in,out] schematic        Schematic on show, whose selection says what applies.
 * @param[in]     is_editing_block  Whether a block is open.
 * @return  The command picked, or none; it runs on the next frame of the editor.
 */
std::optional<EditorCommand> DrawBlockItems(Schematic &schematic, const bool is_editing_block) {
    CommandItems items;
    if (!is_editing_block) {
        items.Draw("Create Block from Selection...", EditorCommand::CREATE_BLOCK, schematic.HasSelection());
    }
    const UIElement *selected = schematic.GetSelectedElement();
    const bool block_selected =
        selected != nullptr && selected->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT;
    items.Draw("Edit Block Contents", EditorCommand::EDIT_BLOCK, block_selected);
    if (is_editing_block) {
        items.Draw("Block Pin and Parameter Settings...", EditorCommand::OPEN_BLOCK_SETTINGS);
        items.Draw("Apply Block and Return", EditorCommand::APPLY_BLOCK);
        items.Draw("Discard Block Changes", EditorCommand::DISCARD_BLOCK);
    }
    return items.GetPicked();
}

} // namespace GUI
