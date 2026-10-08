/**
 * @file    editor_window.h
 * @brief   Schematic editor window with a pannable and zoomable grid canvas.
 */

#ifndef IMCSIM_EDITOR_WINDOW_H
#define IMCSIM_EDITOR_WINDOW_H

#include "components/component.h"
#include "examples.h"
#include "helpers.h"
#include "imgui.h"
#include "schematic.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include "windows/app_window.h"
#include <SDL3/SDL_dialog.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   EditorWindow
 * @brief   Window where the schematic is drawn and edited: components are placed and wired on a grid.
 * @details Positions are stored in world units (one unit per grid cell) and converted to screen pixels
 *          using the current pan offset and zoom factor. The editor is either selecting (the default), placing
 *          a component or drawing wires; starting one mode leaves the other. Schematics are saved to and opened
 *          from JSON files through the native file dialogs of SDL. The schematic itself belongs to the
 *          application; this window only keeps interaction state such as the current mode, drag and view.
 */
class EditorWindow : public AppWindow {
public:
    explicit EditorWindow(Schematic &schematic);
    ~EditorWindow() override = default;

    // Quitting
    void RequestQuit();
    bool IsQuitConfirmed() const;

    // Examples
    void RequestExample(const Example &example);

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
     * @enum    FileAction
     * @brief   File operation waiting for a file dialog or for the user to confirm discarding changes.
     */
    enum class FileAction {
        New,
        Open,
        Save,
        Quit,
        Example
    };

    /**
     * @struct  DialogResult
     * @brief   What the file dialog returned: a path, or no value when it was canceled or failed.
     */
    struct DialogResult {
        std::optional<std::filesystem::path> Path;
    };

    /**
     * @struct  DialogChannel
     * @brief   Where the file dialog callback leaves its result for the main thread.
     * @details Shared between the editor and each pending dialog, so a dialog that answers after the editor is
     *          gone, such as when the application quits with a dialog open, writes to memory that still exists.
     */
    struct DialogChannel {
        // SDL may call the dialog callback from another thread
        std::mutex Mutex;
        std::optional<DialogResult> Result;
    };

    Schematic &m_Schematic;

    static constexpr float DefaultZoom = 20.0f;

    // View
    ImVec2 m_Pan = {0, 0};
    float m_Zoom = DefaultZoom;
    SymbolStyle m_SymbolStyle = SymbolStyle::IEC;
    WireColoring m_WireColoring = WireColoring::Plain;
    // Set when a schematic is opened; the next Draw() frames it, once the canvas size is known
    bool m_FrameRequested = false;

    // Placement
    std::optional<Core::ComponentType> m_PlacingType;
    Rotation m_PlacingRotation = Rotation::R0;
    bool m_PlacingMirrored = false;

    // Wiring
    bool m_DrawingWires = false;
    std::optional<GridPoint> m_WireStart;
    bool m_WireVerticalFirst = false;

    // Probing: picks what the plots measure
    bool m_Probing = false;

    // Selection and dragging
    std::optional<ElementDrag> m_Drag;

    // Quitting: requested by the application between frames, handled by the next Draw()
    bool m_QuitRequested = false;
    bool m_QuitConfirmed = false;

    // Examples: requested from the menu and handled by the next Draw(), then kept while discarding is confirmed
    std::optional<Example> m_RequestedExample;
    std::optional<Example> m_ExampleToOpen;

    // Files: the dialog in progress and the confirmation are touched only by the main thread
    std::optional<FileAction> m_ActionToConfirm;
    std::optional<FileAction> m_DialogAction;
    std::string m_DialogLocation;
    std::shared_ptr<DialogChannel> m_DialogChannel = std::make_shared<DialogChannel>();
    std::string m_FileMessagesTitle;
    std::vector<std::string> m_FileMessages;

    // Frame
    void Draw() override;
    void DrawToolbar();
    void DrawWires(ImDrawList *draw_list, const ViewTransform &view);
    void DrawWireCurrents(ImDrawList *draw_list, const ViewTransform &view,
                          const Core::OperatingPoint &operating_point);
    ImU32 GetElementColor(std::size_t index) const;
    void DrawColorLegend(ImDrawList *draw_list, ImVec2 origin, ImVec2 size) const;
    void DrawNodeVoltages(ImDrawList *draw_list, const ViewTransform &view);
    void DrawMeasurements(ImDrawList *draw_list, const ViewTransform &view);
    void DrawHoveredValue(const ViewTransform &view, bool hovered);
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);
    void FrameSchematic(ImVec2 canvas_size);

    // Modes
    void StartPlacing(Core::ComponentType type);
    void StartDrawingWires();
    void StartProbing();

    // Placement
    void HandlePlacement(ImDrawList *draw_list, const ViewTransform &view, bool hovered);

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

    // File and history commands
    void HandleFileShortcuts();
    void Undo();
    void Redo();
    void RequestNew();
    void RequestOpen();
    void HandleQuitRequest();
    void HandleExampleRequest();
    void Save();
    void NewSchematic();
    void OpenFile(const std::filesystem::path &path);
    void OpenExample(const Example &example);
    void SaveFile(std::filesystem::path path);

    // File dialogs and messages
    void ShowFileDialog(FileAction action);
    static void SDLCALL HandleFileDialogResult(void *userdata, const char *const *file_list, int filter);
    void ProcessDialogResult();
    void DrawFilePopups();
    void ShowFileMessages(std::string title, std::vector<std::string> messages);
};

} // namespace GUI

#endif // IMCSIM_EDITOR_WINDOW_H
