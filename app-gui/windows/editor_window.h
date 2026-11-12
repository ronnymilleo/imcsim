/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "analysis/analysis_controls.h"
#include "analysis/analysis_settings_panel.h"
#include "base/helpers.h"
#include "circuit/components/component.h"
#include "imgui.h"
#include "overlays/glow_renderer.h"
#include "overlays/schematic_overlays.h"
#include "results/playback.h"
#include "results/result_store.h"
#include "schematic/block_session.h"
#include "schematic/canvas_view.h"
#include "schematic/clipboard.h"
#include "schematic/editor_command.h"
#include "schematic/editor_mode.h"
#include "schematic/examples.h"
#include "schematic/preferences.h"
#include "schematic/probing.h"
#include "schematic/schematic.h"
#include "schematic/schematic_file_actions.h"
#include "schematic/selection.h"
#include "schematic/wire_editing.h"
#include "ui_elements/ui_element.h"
#include "windows/app_window.h"
#include "windows/editor_context_menu.h"
#include "windows/editor_menus.h"
#include "windows/editor_toolbar.h"
#include "windows/live_input_handler.h"
#include "windows/part_editor.h"
#include "windows/part_picker.h"
#include "windows/part_popover.h"
#include "windows/part_rail.h"
#include "windows/spice_block_dialog.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @class   EditorWindow
 * @brief   Window where the schematic is drawn and edited: components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is in one EditorMode at a time: selecting (the
 *          default), placing a part, drawing wires, probing or pasting; starting one leaves the others. Modes start
 *          from the EditorToolbar, their keys, or the PartPicker that Space opens, which only ask for them; a status
 *          bar lists the keys of each mode.
 * Schematics are saved to and opened from JSON files by its SchematicFileActions, the colors of a result are drawn by
 * its SchematicOverlays, and where it looks is kept by its CanvasView. The blocks it has open are a BlockSession, and
 * the input of each mode, the menus, the properties popover and the context menu live in modules of their own. The root
 * schematic belongs to the application. While a subcircuit is open, this window edits a temporary document and applies
 * it back to the root as one undo step; it otherwise keeps interaction state such as the current mode and drag.
 */
class EditorWindow : public AppWindow {
public:
    EditorWindow(Schematic &schematic, const ResultStore &results, AnalysisControls &controls,
                 AnalysisSettingsPanel &settings_panel, PartEditor &part_editor, GlowRenderer &glow_renderer,
                 TransientPlayback &playback, const Preferences &preferences);
    ~EditorWindow() override = default;

    // Requests, global shortcuts and file popups, whichever window is in front
    void Update();

    // Command bar, drawn by the application under the menu bar
    void DrawCommandBar();

    // Quitting
    void RequestQuit();
    bool IsQuitConfirmed() const;

    // Examples
    void RequestExample(const Example &example);
    void RequestInsertExample(const Example &example);
    void RequestImport(std::filesystem::path path);

    // Menus, drawn inside the main menu bar; their commands run on the next Update()
    void DrawFileMenuItems();
    void DrawEditMenuItems();
    void DrawViewMenuItems();

private:
    /**
     * @struct  TimeSpan
     * @brief   A stretch of time of the transient, in seconds.
     */
    struct TimeSpan {
        double From = 0.0;
        double To = 0.0;
    };

    Schematic &m_RootSchematic;
    Schematic *m_Schematic;
    /// The results of the root schematic, the only one an analysis runs on.
    const ResultStore &m_RootResults;
    /// What the editor shows of the results while a block is open: none, since a block has no run of its own.
    const ResultStore m_NoResults{};
    /// The blocks open, from the outermost to the one shown, and the settings of the dialog that edits them.
    BlockSession m_Blocks;
    /// Imports SPICE libraries and sets up the pins of the blocks they become.
    SpiceBlockDialog m_SpiceDialog;
    AnalysisControls &m_Controls;
    AnalysisSettingsPanel &m_SettingsPanel;
    PartEditor &m_PartEditor;
    TransientPlayback &m_Playback;
    /// How the schematic is drawn and exported.
    const Preferences &m_Preferences;

    // Parts of the editor, in the order they are built
    CanvasView m_View;
    SchematicFileActions m_Files;
    EditorToolbar m_Toolbar;
    PartPicker m_PartPicker;
    PartRail m_PartRail;
    SchematicOverlays m_Overlays;

    /// What clicks and keys on the canvas do now.
    EditorMode m_Mode;
    /// Whether wires go up or down before they turn, kept from one wire to the next.
    bool m_WireVerticalFirst = false;
    /// Live runs: what the mouse does to the parts set by hand, and the push button it holds.
    LiveInputHandler m_Live;
    /// Properties of the selected part, over the canvas.
    PartPopover m_Popover;
    /// What a right click opens.
    EditorContextMenu m_ContextMenu;
    /// ImGui closes a popup on Esc before the frame starts, so the editor checks the last frame to leave that
    /// Esc alone.
    bool m_PopupOpenLastFrame = false;

    /// Menu command waiting for the next Update().
    std::optional<EditorCommand> m_RequestedCommand;

    // Frame
    void Draw() override;
    static ImVec2 GetCanvasSize();
    void HandleModeKeys();
    void ApplyToolbarRequest(ToolbarRequest request);
    std::optional<TimeSpan> GetWindowSpan() const;
    std::optional<double> GetShownInstant() const;

    // Modes
    void SetMode(EditorMode mode);

    // Blocks
    bool IsEditingSubcircuit() const;
    const ResultStore &GetShownResults() const;
    bool LeaveSubcircuitForDocument();
    bool ReturnToLevel(std::size_t depth);
    void ShowSchematic();
    void OfferBlockParameters();
    void RequestEditSelectedSubcircuit();
    void FinishSubcircuitEditing(bool apply);
    void RunBlockMenuChoice(BlockMenuChoice choice);
    void ApplySpiceBlockResult(SpiceBlockResult result);

    // Clipboard
    void HandleClipboardShortcuts();
    void CopySelected();
    void CutSelected();
    void StartPasting();
    void StartPastingGroup(PastedGroup group);

    // Selection and dragging
    void HandleSelection(const ViewTransform &view, bool hovered);
    void HandleSelectionClick(SelectingMode &selecting, ImVec2 cursor_world);
    void HandleSelectionDoubleClick();
    void UpdateSelectionBox(SelectingMode &selecting, ImVec2 cursor_world);
    void UpdateSelectionDrag(SelectingMode &selecting, ImVec2 cursor_world);
    void HandleSelectionKeys(const SelectingMode &selecting);

    // Menu commands and shortcuts
    void HandleGlobalShortcuts();
    void Undo();
    void Redo();
    void HandleCommandRequest();
    bool RunFileCommand(EditorCommand command);
    bool RunClipboardCommand(EditorCommand command);
    void RunEditCommand(EditorCommand command);
    void RunTurnCommand(EditorCommand command);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
