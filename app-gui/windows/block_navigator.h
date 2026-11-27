/**
 * @file    block_navigator.h
 * @brief   Moves the editor into and out of the blocks of the document, and keeps what it shows in step.
 */

#ifndef IMCSIM_BLOCK_NAVIGATOR_H
#define IMCSIM_BLOCK_NAVIGATOR_H

#include "imgui.h"
#include "overlays/schematic_overlays.h"
#include "schematic/block_session.h"
#include "schematic/canvas_view.h"
#include "schematic/editor_mode.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file_actions.h"
#include "windows/part_editor.h"
#include "windows/spice_settings_dialog.h"
#include <cstddef>
#include <expected>
#include <string>

namespace GUI {

/**
 * @struct  ShownSchematicFollowers
 * @brief   The parts of the editor that follow the schematic on show, which start over when it changes.
 */
struct ShownSchematicFollowers {
    EditorMode &Mode; ///< The mode of the editor; a drag or a part on its way belongs to the schematic it started on.
    PartEditor &Part;
    SchematicOverlays &Overlays;
    CanvasView &View;
};

/**
 * @class   BlockNavigator
 * @brief   The level the editor shows, the document or a block open inside it, and the ways from one to another.
 * @details It owns the BlockSession, whose innermost open block is the schematic on show, and the settings dialog of
 *          the blocks kept as SPICE text, which open in place of their contents. Entering or leaving a block ends
 *          what the editor was doing on the schematic it leaves and starts the new one unselected and framed. The root
 *          schematic and the followers belong to the editor and must outlive the navigator.
 */
class BlockNavigator {
public:
    BlockNavigator(Schematic &root, const ShownSchematicFollowers &followers);

    // Levels
    bool IsEditingBlock() const;
    Schematic &GetShown();
    const Schematic &GetShown() const;
    BlockSession &GetSession();
    const BlockSession &GetSession() const;

    // Moving between levels
    void EditSelectedBlock();
    void ApplyAndLeaveBlock();
    void DiscardAndLeaveBlock();
    bool ReturnToLevel(std::size_t depth);
    bool ReturnToDocument();

    // Settings
    void OpenBlockSettings();

    // Frame
    void DrawDialogs(SchematicFileActions &files);
    void DrawWayBack(ImVec2 origin);

private:
    BlockSession m_Session;
    SpiceSettingsDialog m_SpiceSettings; ///< Changes the pins and parameter defaults of a block kept as SPICE text.
    ShownSchematicFollowers m_Followers;

    void EndDragOfShown();
    void FinishLevelChange(const std::expected<void, std::string> &changed);
    void EnterShownSchematic();
    void SyncBlockParametersToPartEditor();
};

} // namespace GUI

#endif // IMCSIM_BLOCK_NAVIGATOR_H
