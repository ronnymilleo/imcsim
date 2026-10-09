/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "analysis_controls.h"
#include "components/component.h"
#include "examples.h"
#include "file_dialog.h"
#include "helpers.h"
#include "imgui.h"
#include "part_editor.h"
#include "probing.h"
#include "schematic.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "windows/app_window.h"
#include <filesystem>
#include <optional>
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
 * Schematics are saved to and opened from JSON files through the native file dialogs of SDL. The schematic itself
 * belongs to the application; this window only keeps interaction state such as the current mode, drag and view.
 */
class EditorWindow : public AppWindow {
public:
    EditorWindow(Schematic &schematic, AnalysisControls &controls, PartEditor &part_editor);
    ~EditorWindow() override = default;

    // Requests, global shortcuts and file popups, whichever window is in front
    void Update();

    // Quitting
    void RequestQuit();
    bool IsQuitConfirmed() const;

    // Examples
    void RequestExample(const Example &example);

    // Menus, drawn inside the main menu bar; their commands run on the next Update()
    void DrawFileMenuItems();
    void DrawEditMenuItems();
    void DrawViewMenuItems();

    // Preferences, kept in imgui.ini; call once after the ImGui context exists and before the first frame
    void RegisterSettingsHandler();

private:
    /**
     * @struct  ElementDrag
     * @brief   Schematic state captured when an element drag starts; every frame of the drag is computed from it.
     */
    struct ElementDrag {
        GridPoint StartCursor;
        GridPoint StartPosition;
        std::vector<GridPoint> StartTerminals;
        std::vector<UIWire> StartWires;
    };

    /**
     * @enum    WireColoring
     * @brief   What the colors of wires (and, by current, of parts) show.
     * @details Voltage and Current color by the last operating point, on a heat scale with a legend.
     */
    enum class WireColoring {
        Plain,
        Nodes,
        Voltage,
        Current
    };

    /**
     * @enum    EditorCommand
     * @brief   A command picked in a menu, run by the next Draw() so its popups belong to the editor window.
     */
    enum class EditorCommand {
        New,
        Open,
        Save,
        SaveAs,
        ExportSchematic,
        Undo,
        Redo,
        Rotate,
        Mirror,
        Flip,
        Delete
    };

    /**
     * @enum    FileAction
     * @brief   File operation waiting for a file dialog or for the user to confirm discarding changes.
     */
    enum class FileAction {
        New,
        Open,
        Save,
        Quit,
        Example,
        ExportSchematic
    };

    Schematic &m_Schematic;
    AnalysisControls &m_Controls;
    PartEditor &m_PartEditor;

    static constexpr float DefaultZoom = 20.0f;

    // View
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = DefaultZoom;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    bool m_ShowTerminalNumbers = false;
    WireColoring m_WireColoring = WireColoring::Plain;
    // Largest zoom of a framing the next Draw() does, once the canvas size is known: the default when a schematic is
    // opened, more for Fit
    std::optional<float> m_FrameMaxZoom;

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

    // Selection and dragging
    std::optional<ElementDrag> m_Drag;

    // Properties popover: the corner it hangs from, which side of the part it opens on, and a request from a menu
    ImVec2 m_PartPopoverAnchor = {0.0f, 0.0f};
    float m_PartPopoverPivotX = 0.0f;
    bool m_PartPopoverRequested = false;
    // Context menu: what the right click was on, for its measure item; the part picker it can open
    std::optional<MeasurementTarget> m_ContextTarget;
    bool m_PartPickerRequested = false;
    // ImGui closes a popup on Esc before the frame starts, so the editor checks the last frame to leave that Esc alone
    bool m_PopupOpenLastFrame = false;

    // Quitting: requested by the application between frames, handled by the next Draw()
    bool m_QuitRequested = false;
    bool m_QuitConfirmed = false;

    // Menu command waiting for the next Draw()
    std::optional<EditorCommand> m_RequestedCommand;

    // Examples: requested from the menu and handled by the next Draw(), then kept while discarding is confirmed
    std::optional<Example> m_RequestedExample;
    std::optional<Example> m_ExampleToOpen;

    // Files: the dialog in progress and the confirmation are touched only by the main thread
    std::optional<FileAction> m_ActionToConfirm;
    std::optional<FileAction> m_DialogAction;
    FileDialog m_FileDialog;
    std::string m_FileMessagesTitle;
    std::vector<std::string> m_FileMessages;
    // Export: dark keeps the colors of the editor, light suits print
    bool m_ExportDark = false;

    // Frame
    void Draw() override;
    void DrawToolbar();
    void DrawPartButton(Core::ComponentType type);
    void DrawPartGroup(std::size_t index);
    void DrawRunControls();
    void DrawWires(SchematicCanvas &canvas, const ViewTransform &view);
    void DrawWireCurrents(SchematicCanvas &canvas, const ViewTransform &view,
                          const Core::OperatingPoint &operating_point);
    ImU32 GetElementColor(std::size_t index) const;
    void DrawStatusBar(const ViewTransform &view, bool hovered);
    float DrawRunStatus(float right_edge);
    void DrawColorLegend(ImDrawList *draw_list, ImVec2 origin, ImVec2 size) const;
    void DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view);
    void DrawHoveredValue(const ViewTransform &view, bool hovered);
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);
    void FrameSchematic(ImVec2 canvas_size, float max_zoom);

    // Modes
    void StartSelecting();
    void StartPlacing(Core::ComponentType type);
    void StartDrawingWires();
    void StartProbing();

    // Placement
    void DrawPartPicker();
    void HandlePlacement(SchematicCanvas &canvas, const ViewTransform &view, bool hovered);

    // Wiring
    void HandleWireDrawing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);
    void StopWire();

    // Probing
    void HandleProbing(ImDrawList *draw_list, const ViewTransform &view, bool hovered);

    // Selection and dragging
    void HandleSelection(const ViewTransform &view, bool hovered);
    void SelectAt(ImVec2 world_pos);
    void ClearSelection();
    void EndDrag();
    void RotateSelectedElement();
    void MirrorSelectedElement(bool vertically);

    // Properties popover and context menu
    void PlacePartPopover(const ViewTransform &view);
    void HandleValueTyping(const ViewTransform &view);
    void DrawPartPopover();
    void HandleContextMenu(const ViewTransform &view, bool hovered);
    void DrawContextMenu();

    // File and history commands
    void HandleGlobalShortcuts();
    void Undo();
    void Redo();
    void RequestNew();
    void RequestOpen();
    void HandleQuitRequest();
    void HandleExampleRequest();
    void HandleCommandRequest();
    void Save();
    void NewSchematic();
    void OpenFile(const std::filesystem::path &path);
    void OpenExample(const Example &example);
    void SaveFile(std::filesystem::path path);

    // File dialogs and messages
    void ShowFileDialog(FileAction action);
    void ProcessDialogResult();
    void DrawFilePopups();
    void DrawExportPopup();
    void ExportSchematic(std::filesystem::path path);
    void ShowFileMessages(std::string title, std::vector<std::string> messages);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
