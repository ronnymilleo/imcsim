/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "analysis_controls.h"
#include "analysis_settings_panel.h"
#include "canvas_view.h"
#include "circuit_snapshot.h"
#include "clipboard.h"
#include "components/component.h"
#include "editor_preferences.h"
#include "examples.h"
#include "glow_renderer.h"
#include "helpers.h"
#include "imgui.h"
#include "part_editor.h"
#include "playback.h"
#include "probing.h"
#include "schematic.h"
#include "schematic_file_actions.h"
#include "selection.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "windows/app_window.h"
#include "wire_editing.h"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   EditorWindow
 * @brief   Window where the schematic is drawn and edited: components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is either selecting (the default), placing
 *          a component, drawing wires or probing; starting one mode leaves the others. Modes start from the icon
 *          toolbar, their keys, or the part search that Space opens; a status bar lists the keys of each mode.
 * Schematics are saved to and opened from JSON files by its SchematicFileActions, how it looks is kept by its
 * EditorPreferences, and where it looks by its CanvasView. The schematic itself belongs to the application; this window
 * only keeps interaction state such as the current mode and drag.
 */
class EditorWindow : public AppWindow {
public:
    EditorWindow(Schematic &schematic, AnalysisControls &controls, AnalysisSettingsPanel &settings_panel,
                 PartEditor &part_editor, GlowRenderer &glow_renderer, TransientPlayback &playback);
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

    // Preferences, kept in imgui.ini; call once after the ImGui context exists and before the first frame
    void RegisterSettingsHandler();

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
     * @struct  GroupDrag
     * @brief   Schematic state captured when a drag of the selection starts; every frame of the drag is computed from
     * it.
     * @details A single wire only slides across itself, so Slide tells which way it may go.
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
        // Moved the mouse far enough to change something, and what a click without a move leaves selected
        bool Moved = false;
        GridPoint LastCursor;
        GridPoint LastDelta;
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
     * @struct  DisplayedState
     * @brief   The voltages, currents and powers the schematic shows this frame, and the scale their colors use.
     * @details It is the last operating point, or the last transient read at the instant of the playback; the
     *          drawing code does not tell them apart. Time is set for a transient only.
     */
    struct DisplayedState {
        Core::OperatingPoint Point;
        HeatScale Scale;
        std::vector<ComponentPower> Powers;
        std::optional<double> Time;
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

    Schematic &m_Schematic;
    AnalysisControls &m_Controls;
    AnalysisSettingsPanel &m_SettingsPanel;
    PartEditor &m_PartEditor;
    GlowRenderer &m_GlowRenderer;
    TransientPlayback &m_Playback;

    // Parts of the editor, in the order they are built: the files export what the preferences pick
    EditorPreferences m_Preferences;
    CanvasView m_View;
    SchematicFileActions m_Files;

    // What the schematic shows, resolved at the start of each frame; the scale of a transient is worked out once per
    // result and kept
    std::optional<DisplayedState> m_Displayed;
    std::optional<std::size_t> m_ScaledTransientVersion;
    // The window of points the scale was worked out over
    PointWindow m_ScaledWindow;
    // When the heat scale of a transient that is still being made was last worked out
    double m_LastScaleTime = 0.0;
    HeatScale m_TransientScale;

    // Placement
    std::optional<Core::ComponentType> m_PlacingType;
    Rotation m_PlacingRotation = Rotation::R0;
    bool m_PlacingMirrored = false;
    // Part each toolbar group places, the last one picked from its list
    std::vector<Core::ComponentType> m_PartGroupChoices;
    // Part picker: the typed query and the highlighted match
    std::string m_PartQuery;
    std::size_t m_PartPickerIndex = 0;

    // Wiring
    bool m_DrawingWires = false;
    std::optional<GridPoint> m_WireStart;
    bool m_WireVerticalFirst = false;

    // Probing: picks what the plots measure
    bool m_Probing = false;

    // Pasting: what was read from the clipboard, following the cursor until a click places it
    std::optional<PastedGroup> m_Pasting;

    // Selection and dragging
    std::optional<GroupDrag> m_Drag;
    std::optional<SelectionBox> m_Box;

    // Live runs: the push button the mouse holds down
    std::optional<std::size_t> m_HeldButton;

    // Properties popover: the corner it hangs from, which side of the part it opens on, and a request from a menu
    ImVec2 m_PartPopoverAnchor = {0.0f, 0.0f};
    float m_PartPopoverPivotX = 0.0f;
    bool m_PartPopoverRequested = false;
    // Context menu: what the right click was on, for its measure item; the part picker it can open
    std::optional<MeasurementTarget> m_ContextTarget;
    bool m_PartPickerRequested = false;
    // ImGui closes a popup on Esc before the frame starts, so the editor checks the last frame to leave that Esc alone
    bool m_PopupOpenLastFrame = false;

    // Menu command waiting for the next Update()
    std::optional<EditorCommand> m_RequestedCommand;

    // Frame
    void Draw() override;
    void DrawToolbar();
    void DrawPartButton(Core::ComponentType type);
    void DrawRunControls();
    void DrawAnalysisButton(float run_left);
    void DrawLiveToggle();
    void DrawPartGroup(std::size_t index);
    void UpdateDisplayedState();
    std::optional<TimeSpan> GetWindowSpan() const;
    void DrawTimeBar();
    void DrawWires(SchematicCanvas &canvas, const ViewTransform &view);
    void DrawWireSelectionHalos(SchematicCanvas &canvas, const ViewTransform &view);
    void DrawNodeNumbers(SchematicCanvas &canvas, const ViewTransform &view);
    ImU32 GetWireColor(int node) const;
    void DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view);
    void DrawGlows(ImDrawList *draw_list, const ViewTransform &view);
    ImU32 GetElementColor(std::size_t index) const;
    bool AreWiresHeatColored() const;
    bool ArePartsHeatColored() const;
    void DrawStatusBar(const ViewTransform &view, bool hovered);
    float DrawRunStatus(float right_edge);
    void DrawColorLegend(ImDrawList *draw_list, ImVec2 origin, ImVec2 size) const;
    void DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view);
    void DrawHoveredValue(const ViewTransform &view, bool hovered);

    // Modes
    void StartSelecting();
    void StartPlacing(Core::ComponentType type);
    void StartDrawingWires();
    void StartProbing();
    void StopEditing();

    // Placement
    void DrawPartPicker();
    void HandlePlacement(SchematicCanvas &canvas, const ViewTransform &view, bool hovered);

    // Wiring
    void HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
    void StopWire();

    // Probing
    void HandleProbing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);

    // Clipboard
    void HandleClipboardShortcuts();
    void CopySelected();
    void CutSelected();
    void StartPasting();
    void StartPastingGroup(PastedGroup group);
    void HandlePasting(SchematicCanvas &canvas, const ViewTransform &view, bool hovered);
    bool IsSelecting() const;
    std::string FindLastNet() const;

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
    void PlacePartPopover(const ViewTransform &view);
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
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
