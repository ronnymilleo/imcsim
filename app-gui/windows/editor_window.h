/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "analysis/analysis_controls.h"
#include "analysis/analysis_settings_panel.h"
#include "analysis/playback.h"
#include "components/component.h"
#include "document/clipboard.h"
#include "document/examples.h"
#include "document/schematic.h"
#include "document/schematic_file_actions.h"
#include "drawing/glow_renderer.h"
#include "drawing/schematic_overlays.h"
#include "editing/canvas_view.h"
#include "editing/editor_mode.h"
#include "editing/probing.h"
#include "editing/selection.h"
#include "editing/wire_editing.h"
#include "helpers.h"
#include "imgui.h"
#include "part_editor.h"
#include "preferences.h"
#include "ui_elements/ui_element.h"
#include "windows/app_window.h"
#include "windows/editor_toolbar.h"
#include "windows/part_picker.h"
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
 * its SchematicOverlays, and where it looks is kept by its CanvasView. The root schematic belongs to the application.
 * While a subcircuit is open, this window edits a temporary document and applies it back to the root as one undo step;
 * it otherwise keeps interaction state such as the current mode and drag.
 */
class EditorWindow : public AppWindow {
public:
    EditorWindow(Schematic &schematic, AnalysisControls &controls, AnalysisSettingsPanel &settings_panel,
                 PartEditor &part_editor, GlowRenderer &glow_renderer, TransientPlayback &playback,
                 const Preferences &preferences);
    ~EditorWindow() override = default;

    // Requests, global shortcuts and file popups, whichever window is in front
    void Update();

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

    /**
     * @struct  OpenBlock
     * @brief   A block whose contents the editor opened, on the way down from the schematic.
     * @details Pins and Parameters hold its settings while a block inside it is open; the innermost block keeps them in
     *          the dialog fields instead.
     */
    struct OpenBlock {
        std::string Name;
        std::unique_ptr<Schematic> Content;
        std::vector<SavedSubcircuitPin> Pins;
        std::vector<Core::SubcircuitParameter> Parameters;
    };

    /**
     * @enum    EditorCommand
     * @brief   A command picked in a menu, run by the next Draw() so its popups belong to the editor window.
     */
    enum class EditorCommand {
        NEW,
        OPEN,
        SAVE,
        SAVE_AS,
        IMPORT,
        IMPORT_SPICE,
        EXPORT_SCHEMATIC,
        UNDO,
        REDO,
        ROTATE,
        MIRROR,
        FLIP,
        ROTATE_KEEP_WIRES,
        MIRROR_KEEP_WIRES,
        FLIP_KEEP_WIRES,
        CUT,
        COPY,
        PASTE,
        SELECT_ALL,
        DELETE_SELECTION
    };

    Schematic &m_RootSchematic;
    Schematic *m_Schematic;
    /// The blocks open, from the outermost to the one shown; empty while the schematic is shown.
    std::vector<OpenBlock> m_OpenBlocks;
    std::vector<SavedSubcircuitPin> m_SubcircuitPins;
    std::vector<Core::SubcircuitParameter> m_SubcircuitParameters;
    std::string m_SubcircuitName;
    std::string m_SubcircuitError;
    bool m_SubcircuitDialogRequested = false;
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
    SchematicOverlays m_Overlays;

    /// What clicks and keys on the canvas do now.
    EditorMode m_Mode;
    /// Whether wires go up or down before they turn, kept from one wire to the next.
    bool m_WireVerticalFirst = false;
    /// Live runs: the push button the mouse holds down.
    std::optional<std::size_t> m_HeldButton;

    // Properties popover
    /// The point the middle of the top edge of the popover opens on.
    ImVec2 m_PartPopoverAnchor = {0.0f, 0.0f};
    /// The middle of the canvas on screen, as drawn last.
    ImVec2 m_CanvasCenter = {0.0f, 0.0f};
    /// The top edge of the canvas on screen, as drawn last.
    float m_CanvasTop = 0.0f;
    /// Whether a menu asked to open the popover.
    bool m_PartPopoverRequested = false;
    // Context menu
    /// What the right click was on, for the measure item of the context menu.
    std::optional<MeasurementTarget> m_ContextTarget;
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
    void DrawShownInstant(ImDrawList *draw_list, ImVec2 origin) const;
    void DrawBlockBar(ImVec2 origin);
    void DrawStatusBar(const ViewTransform &view, bool hovered);
    std::optional<MeasurementTarget> FindTargetAt(const ViewTransform &view) const;

    // Modes
    void SetMode(EditorMode mode);
    SelectingMode *GetSelecting();
    bool IsDragging() const;
    void StopEditing();

    // Blocks
    bool IsEditingSubcircuit() const;
    bool LeaveSubcircuitForDocument();
    bool ReturnToLevel(std::size_t depth);
    void ShowSchematic(Schematic &schematic);
    bool CanPlaceBlock(std::string_view name) const;
    std::vector<const SavedSubcircuit *> ListPlaceableBlocks() const;
    void SyncOpenBlockDefinitions();
    void OfferBlockParameters();
    void RequestCreateSubcircuit();
    void RequestEditSelectedSubcircuit();
    bool SyncSubcircuitPins();
    bool ApplySubcircuitPinNames();
    void FinishSubcircuitEditing(bool apply);
    void DrawSubcircuitDialog();
    void DrawSubcircuitPinRows(bool editing);
    void DrawSubcircuitParameterRows();
    void DrawSubcircuitDialogButtons(bool editing);
    void ApplySpiceBlockResult(SpiceBlockResult result);

    // Input of each mode, after the schematic is drawn; each returns the mode to switch to, or none
    std::optional<EditorMode> HandleMode(SelectingMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                         bool hovered);
    std::optional<EditorMode> HandleMode(PlacingMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                         bool hovered);
    std::optional<EditorMode> HandleMode(WiringMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                         bool hovered);
    std::optional<EditorMode> HandleMode(ProbingMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                         bool hovered);
    std::optional<EditorMode> HandleMode(PastingMode &mode, SchematicCanvas &canvas, const ViewTransform &view,
                                         bool hovered);
    bool HandlePastingKeys(PastedGroup &group);
    void PlacePastedGroup(PastedGroup &group);
    std::string FindLastNet() const;

    // Clipboard
    void HandleClipboardShortcuts();
    void CopySelected();
    void CutSelected();
    void StartPasting();
    void StartPastingGroup(PastedGroup group);

    // Selection and dragging
    void HandleSelection(const ViewTransform &view, bool hovered);
    void ApplySelection(const Selection &selection);
    void ClearSelection();
    void SelectAll();
    GroupDrag MakeDrag(GridPoint cursor, std::optional<Selection> collapse_to) const;
    void UpdateDrag(GridPoint cursor);
    void EndDrag();
    void DrawSelectionBox(ImDrawList *draw_list, const ViewTransform &view) const;
    void TransformSelection(PointTransform::Kind kind, bool keep_wires);

    // Live runs
    std::optional<std::size_t> FindLivePart(const ViewTransform &view) const;
    bool HandleLiveInput(std::optional<std::size_t> live_part);
    void ReleaseHeldButton();

    // Properties popover and context menu
    void PlacePartPopover();
    void HandleValueTyping(const ViewTransform &view);
    void DrawPartPopover();
    void HandleContextMenu(const ViewTransform &view, bool hovered);
    void DrawContextMenu();

    // Menu commands and shortcuts
    void HandleGlobalShortcuts();
    void Undo();
    void Redo();
    void HandleCommandRequest();
    bool RunFileCommand(EditorCommand command);
    bool RunClipboardCommand(EditorCommand command);
    void RunEditCommand(EditorCommand command);
    void RunTurnCommand(EditorCommand command);
    void DrawTurnMenuItems(bool part_selected);
    void DrawBlockMenuItems();
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
