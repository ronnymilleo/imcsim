/**
 * @file    block_navigator.cpp
 * @brief   Moves the editor into and out of the blocks of the document, and keeps what it shows in step.
 */

#include "windows/block_navigator.h"

#include "circuit/subcircuit.h"
#include "schematic/selection_editing.h"
#include "windows/block_dialog.h"
#include <optional>
#include <utility>
#include <vector>

namespace GUI {

/**
 * @brief   Creates the navigator on the document, which it shows until a block opens.
 * @param[in] root       The document; it must outlive the navigator.
 * @param[in] followers  The parts of the editor that follow the schematic on show; they must outlive the navigator.
 */
BlockNavigator::BlockNavigator(Schematic &root, const ShownSchematicFollowers &followers)
    : m_Session(root), m_Followers(followers) {
}

/**
 * @brief   Tells whether a block is open, so the editor shows it instead of the document.
 */
bool BlockNavigator::IsEditingBlock() const {
    return m_Session.IsOpen();
}

/**
 * @brief   The schematic on show: the innermost open block, or the document.
 */
Schematic &BlockNavigator::GetShown() {
    return m_Session.GetShown();
}

/**
 * @brief   The schematic on show: the innermost open block, or the document.
 */
const Schematic &BlockNavigator::GetShown() const {
    return m_Session.GetShown();
}

/**
 * @brief   The open blocks and the settings of their dialog, for the parts of the editor that place or make blocks.
 */
BlockSession &BlockNavigator::GetSession() {
    return m_Session;
}

/**
 * @brief   The open blocks and the settings of their dialog.
 */
const BlockSession &BlockNavigator::GetSession() const {
    return m_Session;
}

/**
 * @brief   Opens the block selected where the editor is, which may be inside another block, or its settings when it is
 *          kept as SPICE text.
 * @details A block kept as SPICE text has no drawing to open, only its pins and parameters to set.
 */
void BlockNavigator::EditSelectedBlock() {
    const std::optional<std::string> name = BlockSession::FindBlockName(GetShown().GetSelectedElement());
    if (!name) {
        return;
    }
    const SavedSubcircuit *definition = m_Session.FindDefinition(*name);
    if (definition != nullptr && definition->Spice) {
        m_SpiceSettings.Open(*definition);
        return;
    }
    EndDragOfShown();
    FinishLevelChange(m_Session.Open(*name));
}

/**
 * @brief   Applies the innermost open block to the document and shows the level above it; a block that cannot be
 *          applied stays open, with the reason in the block dialog.
 */
void BlockNavigator::ApplyAndLeaveBlock() {
    EndDragOfShown();
    FinishLevelChange(m_Session.ApplyAndClose());
}

/**
 * @brief   Drops the changes of the innermost open block and shows the level above it.
 */
void BlockNavigator::DiscardAndLeaveBlock() {
    EndDragOfShown();
    FinishLevelChange(m_Session.DiscardAndClose());
}

/**
 * @brief   Applies the open blocks, innermost first, until as many are left open as asked.
 * @param[in] depth  How many blocks to leave open; 0 goes back to the document.
 * @return  True once there; false when a block cannot be applied, which stops there with the reason shown.
 */
bool BlockNavigator::ReturnToLevel(const std::size_t depth) {
    while (m_Session.GetOpenBlocks().size() > depth) {
        const std::size_t open = m_Session.GetOpenBlocks().size();
        ApplyAndLeaveBlock();
        if (m_Session.GetOpenBlocks().size() == open) {
            return false;
        }
    }
    return true;
}

/**
 * @brief   Applies every open block, innermost first, before the document is replaced or the program quits, so their
 *          edits count as unsaved changes of the document instead of vanishing with it.
 * @return  True once the document is on show; false when a block cannot be applied, which stays open with the reason.
 */
bool BlockNavigator::ReturnToDocument() {
    return ReturnToLevel(0);
}

/**
 * @brief   Opens the dialog on the settings of the open block, or of the block to make from the selection, with the
 *          pins as the pin markers stand now.
 */
void BlockNavigator::OpenBlockSettings() {
    if (const auto synced = m_Session.SyncPins(); !synced) {
        m_Session.ReportError(synced.error());
    }
    m_Session.RequestDialog();
}

/**
 * @brief   Draws the block dialog and the settings dialog of the blocks kept as SPICE text, and applies what they
 *          confirm.
 * @param[in,out] files  Where a refused change of a SPICE block is told.
 * @note    Call it every frame from the same place, outside any window, so the popups open where they are drawn.
 */
void BlockNavigator::DrawDialogs(SchematicFileActions &files) {
    if (DrawBlockDialog(m_Session)) {
        SyncBlockParametersToPartEditor();
    }
    if (const std::optional<SpiceSettingsResult> settings = m_SpiceSettings.Draw()) {
        // New settings reach every instance of the block, in the document and in the open blocks
        const auto updated = m_Session.ApplySpiceUpdate(settings->Name, settings->Pins, settings->Parameters);
        if (!updated) {
            files.QueueMessages("Could not change the SPICE block", {updated.error()});
        }
    }
}

/**
 * @brief   Draws the way back from the open blocks over the canvas, and goes back to the level the user picks in it.
 * @param[in] origin  Top left corner of the canvas on the screen.
 */
void BlockNavigator::DrawWayBack(const ImVec2 origin) {
    if (const std::optional<std::size_t> picked = DrawBlockBar(m_Session, origin)) {
        ReturnToLevel(*picked);
    }
}

/**
 * @brief   Ends a drag before the schematic on show changes: it belongs to the schematic it started on and keeps
 *          pointers into it, and closing a block destroys that one.
 */
void BlockNavigator::EndDragOfShown() {
    EndDrag(GetShown(), m_Followers.Mode);
}

/**
 * @brief   Shows the level a block was opened or closed to, or tells in the block dialog why it was not.
 */
void BlockNavigator::FinishLevelChange(const std::expected<void, std::string> &changed) {
    if (!changed) {
        m_Session.ReportError(changed.error());
        return;
    }
    EnterShownSchematic();
}

/**
 * @brief   Starts the schematic now on show unselected and framed, with the part editor on it.
 * @details A part or a group on its way would land in the other schematic, so placing and pasting end.
 */
void BlockNavigator::EnterShownSchematic() {
    EditorMode &mode = m_Followers.Mode;
    if (std::holds_alternative<PlacingMode>(mode) || std::holds_alternative<PastingMode>(mode)) {
        mode = SelectingMode{};
    }
    m_Followers.Part.SetSchematic(GetShown());
    if (IsEditingBlock()) {
        SyncBlockParametersToPartEditor();
    } else {
        m_Followers.Part.SetBlockParameters({});
    }
    GetShown().ClearSelection();
    m_Followers.Overlays.Reset();
    m_Followers.View.RequestDefaultFrame();
}

/**
 * @brief   Gives the part editor the names of the parameters in the settings of the open block, as they stand, so its
 *          parts can follow them.
 */
void BlockNavigator::SyncBlockParametersToPartEditor() {
    std::vector<std::string> names;
    for (const Core::SubcircuitParameter &parameter : m_Session.GetParameters()) {
        names.push_back(parameter.Name);
    }
    m_Followers.Part.SetBlockParameters(std::move(names));
}

} // namespace GUI
